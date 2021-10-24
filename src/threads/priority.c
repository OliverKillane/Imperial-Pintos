#include "threads/priority.h"
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/interrupt.h"

/* Array of all ready lists, each ready list at priority (index + PRI_MIN). */
static struct ready_queue ready_queue_array[1 + PRI_MAX - PRI_MIN];

/* List containing all non-empty ready_queues,
 * in ascending order by priority.
 */
static struct list nonempty_ready_queues;

/* The load-average of the CPU */
static fixed32 load_average;

/* Number of current ready or running threads */
static int32_t num_threads;

static void tqueue_priority_update(struct thread *thread, int8_t new_priority);
static bool ready_queue_cmp(const struct list_elem *a,
														const struct list_elem *b, void *aux UNUSED);

/* General helper functions for the priority donation system */
inline list_less_func donation_thread_less_func;
inline pqueue_less_func donation_lock_less_func;
inline void donation_cascading_update(struct thread *thread, struct lock *lock,
																			bool is_curr_thread);
inline void donation_thread_update_donee(struct thread *thread);
inline void donation_lock_update_donee(struct lock *lock);
inline void donation_thread_update_donor(struct thread *thread);
inline void donation_lock_update_donor(struct lock *lock);

static void mlfqs_init(void);
static void mlfqs_update_priority(struct thread *thread);
static void mlfqs_decay_thread(struct thread *thread);
static void recalculate_load_avg(void);

/* If false (default), use round-robin scheduler.
 * If true, use multi-level feedback queue scheduler.
 * Controlled by kernel command-line option "-mlfqs".
 */
bool thread_mlfqs;

/* Initialises the priority scheduler.
 *
 * Initialises the ready_queues for each priority level, and the linked list
 * which connects the non-empty queues together.
 *
 * If mlfqs is enabled, initialises mlfqs.
 */
void tqueue_init(void)
{
	num_threads = 0;

	for (int index = 0; index <= PRI_MAX - PRI_MIN; index++)
		list_init(&ready_queue_array[index].thread_queue);
	list_init(&nonempty_ready_queues);

	if (thread_mlfqs)
		mlfqs_init();
}

/* Returns the current (not base) priority of the given thread */
int tqueue_get_priority(const struct thread *thread)
{
	return thread->priority.priority;
}

/* Retrieves the next thread to be scheduled without popping it from the queue.
 * If there are no ready threads, returns NULL.
 */
struct thread *tqueue_front(void)
{
	enum intr_level old_level;
	old_level = intr_disable();
	struct thread *front_thread;

	if (list_empty(&nonempty_ready_queues))
		front_thread = NULL;
	else
		front_thread = list_entry(
						list_front(&list_entry(list_front(&nonempty_ready_queues),
																	 struct ready_queue, elem)
																->thread_queue),
						struct thread, elem);

	intr_set_level(old_level);
	return front_thread;
}

/* Pops the next thread to be scheduled from the queue and returns it.
 * It is no longer present in the priority structure.
 * If there are no ready threads, returns NULL.
 */
struct thread *tqueue_next(void)
{
	enum intr_level old_level;
	struct thread *next_thread;
	old_level = intr_disable();

	if (list_empty(&nonempty_ready_queues)) {
		next_thread = NULL;
	} else {
		struct list *thread_queue = &list_entry(list_front(&nonempty_ready_queues),
																						struct ready_queue, elem)
																				 ->thread_queue;
		next_thread = list_entry(list_pop_front(thread_queue), struct thread, elem);
		list_push_back(thread_queue, &next_thread->elem);
	}

	intr_set_level(old_level);
	return next_thread;
}

/* Add a thread to the scheduler with the priority inherited from
 * the parent thread.
 */
void tqueue_thread_init(struct thread *thread, struct thread *parent)
{
	thread->priority.base_priority = parent->priority.base_priority;
	donation_thread_init(thread);
	tqueue_add(thread);
}

/* if A is lower priority than B, return true. Used for inserting ready_queues
 * into nonempty_ready_queues, placing highest priority ready_queues at the
 * front.
 */
static bool ready_queue_cmp(const struct list_elem *a,
														const struct list_elem *b, void *aux UNUSED)
{
	/* higher priorities will be later in the nonempty_ready_queues array,
	 * so I can just compare pointers
	 */
	return a > b;
}

/* add a new thread into the ready threads, according to its priority.
 * Thread must not already be in ready.
 *
 * The thread must not already be in the tqueue data structure.
 */
void tqueue_add(struct thread *thread)
{
	enum intr_level old_level;
	old_level = intr_disable();

	struct ready_queue *r_list =
					&ready_queue_array[thread->priority.priority - PRI_MIN];

	if (list_empty(&r_list->thread_queue))
		list_insert_ordered(&nonempty_ready_queues, &r_list->elem, ready_queue_cmp,
												NULL);

	/* Add thread to the correct ready_queue */
	list_push_back(&r_list->thread_queue, &thread->elem);

	num_threads++;

	intr_set_level(old_level);
}

/* Makes the scheduler acknowledge that the thread has been blocked from
 * the queue.
 *
 * The thread must already be in the tqueue data structure.
 */
void tqueue_remove(struct thread *thread)
{
	enum intr_level old_level;
	old_level = intr_disable();

	struct ready_queue *r_list =
					&ready_queue_array[thread->priority.priority - PRI_MIN];

	list_remove(&thread->elem);

	if (list_empty(&r_list->thread_queue))
		list_remove(&r_list->elem);

	num_threads--;

	intr_set_level(old_level);
}

/* update a thread's position in the priority queue. */
static void tqueue_priority_update(struct thread *thread, int8_t new_priority)
{
	/* interrupts disabled, an interrupt may modify the priority of the thread */
	enum intr_level old_level;
	old_level = intr_disable();

	if (thread->priority.priority != new_priority) {
		tqueue_remove(thread);
		thread->priority.priority = new_priority;
		tqueue_add(thread);
	}

	intr_set_level(old_level);
}

int32_t tqueue_get_size(void)
{
	return num_threads;
}

/* Initializes the values in struct thread_priority inside the thread that
 * correspond to the donation system, as well as the semaphore-based guard for
 * that data
 */
void donation_thread_init(struct thread *thread)
{
	ASSERT(intr_get_level() == INTR_OFF);
	sema_init(&thread->priority_guard, 1);
	thread->priority.priority = thread->priority.base_priority;
	thread->priority.donee = NULL;
	pqueue_init(&thread->priority.donors, donation_lock_less_func, NULL);
}

/* Initializes the values in struct lock_priority inside the lock that
 * correspond to the donation system, as well as the semaphore-based guard for
 * that data
 */
void donation_lock_init(struct lock *lock)
{
	sema_init(&lock->priority_guard, 1);
	lock->priority.priority = PRI_MIN;
	lock->priority.donee = NULL;
	pqueue_elem_init(&lock->priority.elem);
	list_init(&lock->priority.donors);
}

/* Frees the resources of the thread that are related to priority donation */
void donation_thread_destroy(struct thread *thread)
{
	ASSERT(intr_get_level() == INTR_OFF);
	pqueue_destroy(&thread->priority.donors);
}

/* Signifies that the thread is now being blocked by a lock. The thread
 * cannot be blocked by any other lock. The function blocks the access to the
 * data of both thread and the lock.
 */
void donation_thread_block(struct thread *thread, struct lock *lock)
{
	sema_down(&thread->priority_guard);
	sema_down(&lock->priority_guard);

	ASSERT(!thread->priority.donee);
	thread->priority.donee = lock;
	list_insert_ordered(&lock->priority.donors, &thread->priority.elem,
											donation_thread_less_func, NULL);
	donation_cascading_update(thread, NULL, true);
}

/* Signifies that the thread is no longer being blocked by the thread it was
 * marked as blocked by. The thread must be blocked by another lock already.
 * The lock the thread was blocked by must not be acquired by any thread.
 * The function blocks the access to the data of both thread and the lock that
 * it has been blocked by.
 */
void donation_thread_unblock(struct thread *thread)
{
	sema_down(&thread->priority_guard);
	ASSERT(thread->priority.donee);

	struct lock *lock = thread->priority.donee;
	sema_down(&lock->priority_guard);
	ASSERT(!lock->priority.donee);

	list_remove(&thread->priority.elem);
	thread->priority.donee = NULL;
	donation_lock_update_donee(lock);

	sema_up(&thread->priority_guard);
	sema_up(&lock->priority_guard);
}

/* Signifies that the thread has successfully acquired the lock. The lock cannot
 * already be acquired by any other thread. The function blocks the access to
 * the data of both thread and the lock.
 */
void donation_thread_acquire(struct thread *thread, struct lock *lock)
{
	sema_down(&lock->priority_guard);
	sema_down(&thread->priority_guard);
	ASSERT(!lock->priority.donee);
	lock->priority.donee = thread;
	pqueue_push(&lock->priority.donee->priority.donors, &lock->priority.elem);
	donation_cascading_update(NULL, lock, false);
}

/* Signifies that the thread holding the lock has released it. The thread
 * holding the lock cannot be blocked by any other lock. The lock must be
 * already held by some thread. The function blocks the access to the
 * data of both lock and the thread that was acquiring it.
 */
void donation_thread_release(struct lock *lock)
{
	sema_down(&lock->priority_guard);
	ASSERT(lock->priority.donee);

	struct thread *thread = lock->priority.donee;
	sema_down(&thread->priority_guard);
	ASSERT(!lock->priority.donee->priority.donee);

	pqueue_remove(&thread->priority.donors, &lock->priority.elem);
	lock->priority.donee = NULL;
	donation_thread_update_donee(thread);

	sema_up(&lock->priority_guard);
	sema_up(&thread->priority_guard);
}

/* Sets the base priority of the thread. The thread must not be
 * blocked by any other thread. New base priority must be
 * between PRI_MIN and PRI_MAX. The function blocks access to thread's priority
 * data.
 */
void donation_set_base_priority(struct thread *thread, int base_priority)
{
	sema_up(&thread->priority_guard);
	ASSERT(!thread->priority.donee);
	ASSERT(base_priority >= PRI_MIN && base_priority <= PRI_MAX);

	thread->priority.base_priority = base_priority;
	donation_thread_update_donee(thread);

	sema_down(&thread->priority_guard);
}

/* Returns the base priority of the thread */
int donation_get_base_priority(const struct thread *thread)
{
	return thread->priority.base_priority;
}

/* Updates the donated priority of DONATION_MAX_DEPTH threads down the line.
 * It uses hand-over-hand locking to traverse to the subsequent donees. It
 * assumes that the initial donor and its donee already have their guards
 * acquired.
 */
void donation_cascading_update(struct thread *thread, struct lock *lock,
															 bool is_curr_thread)
{
	for (int depth = 0; (depth < DONATION_MAX_DEPTH &&
											 ((is_curr_thread && thread->priority.donee) ||
											 (!is_curr_thread && lock->priority.donee)));
			 depth++, is_curr_thread ^= true) {
		if (is_curr_thread) {
			lock = thread->priority.donee;
			if (depth)
				sema_down(&lock->priority_guard);
			donation_thread_update_donor(thread);
			donation_lock_update_donee(lock);
			sema_up(&thread->priority_guard);
		} else {
			thread = lock->priority.donee;
			if (depth)
				sema_down(&thread->priority_guard);
			donation_lock_update_donor(lock);
			donation_thread_update_donee(thread);
			sema_up(&lock->priority_guard);
		}
	}
	if (is_curr_thread)
		sema_up(&thread->priority_guard);
	else
		sema_up(&lock->priority_guard);
}

/* Comparison function for donor threads stored in the donee lock */
bool donation_thread_less_func(const struct list_elem *a_raw,
															 const struct list_elem *b_raw, void *aux UNUSED)
{
	struct thread *a = list_entry(a_raw, struct thread, priority.elem);
	struct thread *b = list_entry(b_raw, struct thread, priority.elem);
	return a->priority.priority > b->priority.priority;
}

/* Comparison function for the donor locks stored in the donee thread */
bool donation_lock_less_func(const struct pqueue_elem *a_raw,
														 const struct pqueue_elem *b_raw, void *aux UNUSED)
{
	struct lock *a = pqueue_entry(a_raw, struct lock, priority.elem);
	struct lock *b = pqueue_entry(b_raw, struct lock, priority.elem);
	return a->priority.priority > b->priority.priority;
}

/* Updates the calculated priority of the thread based on the donors and the
 * base priority
 */
void donation_thread_update_donee(struct thread *thread)
{
	int8_t new_priority;
	if (pqueue_size(&thread->priority.donors)) {
		new_priority = pqueue_entry(pqueue_top(&thread->priority.donors),
																struct lock, priority.elem)
													 ->priority.priority;
		if (thread->priority.base_priority > new_priority)
			new_priority = thread->priority.base_priority;
	} else {
		new_priority = thread->priority.base_priority;
	}

	tqueue_priority_update(thread, new_priority);
}

/* Updates the calculated priority of the lock based on the donors */
void donation_lock_update_donee(struct lock *lock)
{
	if (list_empty(&lock->priority.donors)) {
		lock->priority.priority = PRI_MIN;
	} else {
		lock->priority.priority = list_entry(list_front(&lock->priority.donors),
																				 struct thread, priority.elem)
																			->priority.priority;
	}
}

/* Updates the value of the donation given to donors of THREAD */
void donation_thread_update_donor(struct thread *thread)
{
	list_remove(&thread->priority.elem);
	list_insert_ordered(&thread->priority.donee->priority.donors,
											&thread->priority.elem, donation_thread_less_func, NULL);
}

/* Updates the value of the donation given to donors of LOCK */
void donation_lock_update_donor(struct lock *lock)
{
	pqueue_update(&lock->priority.donee->priority.donors, &lock->priority.elem);
}

/* Initializes the advanced scheduling calculator */
void mlfqs_init(void)
{
	load_average = int_to_fixed(0);
}

/* update the priority of a thread based on recent_cpu and niceness */
static void mlfqs_update_priority(struct thread *thread)
{
	fixed32 recent_cpu = thread->priority.recent_cpu;
	int8_t niceness = thread->priority.niceness;

	int8_t new_priority =
					PRI_MAX - fixed_to_int_round(div_f_i(recent_cpu, 4)) - (niceness * 2);

	tqueue_priority_update(thread, new_priority);
}

/* Sets the base niceness value */
void mlfqs_set_nice(struct thread *thread, int nice)
{
	thread->priority.niceness = (int8_t)nice;
	mlfqs_update_priority(thread);
}

/* Gets the base niceness value */
int mlfqs_get_nice(const struct thread *thread)
{
	return (int)thread->priority.niceness;
}

/* Increments the recent_cpu of the thread & recalculates its priority */
void mlfqs_tick(struct thread *thread)
{
	thread->priority.recent_cpu = add_f_i(thread->priority.recent_cpu, 1);
	mlfqs_update_priority(thread);
}

/* Returns the recent_cpu of the thread times 100 and rounded to the nearest
 * int.
 */
int mlfqs_get_recent_cpu(const struct thread *thread)
{
	return fixed_to_int_round(mult_f_i(thread->priority.recent_cpu, 100));
}

/* Recalculates the recent_cpu of a thread according to the formula:
 * recent_cpu = (2 * load_avg) / (2 * load_avg + 1) * recent_cpu + niceness
 *
 * Then updates the priority to match.
 */
static void mlfqs_decay_thread(struct thread *thread)
{
	int8_t niceness = thread->priority.niceness;
	fixed32 recent_cpu = thread->priority.recent_cpu;

	/* Fixed Point calculations for new recent_cpu */
	fixed32 twice_load_avg = mult_f_i(load_average, 2);
	fixed32 twice_load_avg_plus_one = add_f_i(twice_load_avg, 1);
	fixed32 load_avg_ratio = div(twice_load_avg, twice_load_avg_plus_one);
	fixed32 discounted_recent_cpu = mult(load_avg_ratio, recent_cpu);
	fixed32 new_recent_cpu = add_f_i(discounted_recent_cpu, niceness);

	thread->priority.recent_cpu = new_recent_cpu;

	mlfqs_update_priority(thread);
}

/* Recalculates the load average according to the formula:
 * load_avg = (59/60) * load_avg + (1/60) * num_threads
 */
static void recalculate_load_avg(void)
{
	fixed32 fifty_nine_sixty = div_f_i(int_to_fixed(59), 60);
	fixed32 one_sixty = div_f_i(int_to_fixed(1), 60);

	/* avoid race condition on load_average */
	enum intr_level old_level;
	old_level = intr_disable();

	load_average = add(mult(fifty_nine_sixty, load_average),
										 mult_f_i(one_sixty, num_threads));

	intr_set_level(old_level);
}

/* Performs exponential average step on all recent_cpu and load_avg */
void mlfqs_decay(void)
{
	struct list *rq_current;

	recalculate_load_avg();

	/* iterate through all ready queues, for each ready queue,
   * call mlfqs_decay_thread on every thread
   */
	LIST_ITER(&nonempty_ready_queues, rq_elem)
	{
		rq_current = &list_entry(rq_elem, struct ready_queue, elem)->thread_queue;

		LIST_ITER(rq_current, t_elem)
		mlfqs_decay_thread(list_entry(t_elem, struct thread, elem));
	}
}

/* Returns the load average times 100 and rounded to the nearest int */
int mlfqs_get_load_avg(void)
{
	return fixed_to_int_round(mult_f_i(load_average, 100));
}
