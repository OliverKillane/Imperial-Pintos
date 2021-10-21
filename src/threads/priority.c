#include <string.h>
#include "threads/interrupt.h"
#include "threads/priority.h"
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
	return thread->priority.donation_node.priority;
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

/* Add a thread to the scheduler with the priority of the parent thread. */
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
					&ready_queue_array[thread->priority.donation_node.priority - PRI_MIN];

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
					&ready_queue_array[thread->priority.donation_node.priority - PRI_MIN];

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

	if (thread->priority.donation_node.priority != new_priority) {
		tqueue_remove(thread);
		thread->priority.donation_node.priority = new_priority;
		tqueue_add(thread);
	}

	intr_set_level(old_level);
}

int32_t tqueue_get_size(void)
{
	return num_threads;
}

/* Initializes the values in struct thread_priority inside the thread that
 * correspond to the donation system
 */
void donation_thread_init(struct thread *thread)
{
	// TODO: better description (like the ones in synch.c)
	// TODO: implementation
}

/* Initializes the values in struct lock_priority inside the lock that
 * correspond to the donation system
 */
void donation_lock_init(struct lock *lock)
{
	// TODO: better description (like the ones in synch.c)
	// TODO: implementation
}

/* Frees the resources of the thread that are related to priority donation */
void donation_thread_destroy(struct thread *thread)
{
	// TODO: better description (like the ones in synch.c)
	// TODO: implementation
}

/* Signifies that the thread is now being blocked by a lock. The thread
 * cannot be blocked by any other lock
 */
void donation_thread_block(struct thread *thread, struct lock *lock)
{
	// TODO: better description (like the ones in synch.c)
	// TODO: implementation
}

/* Signifies that the thread is no longer being blocked by the thread it was
 * marked as blocked by. The thread must be blocked by another lock already.
 * The lock the thread was blocked by must not be acquired by any thread.
 */
void donation_thread_unblock(struct thread *thread)
{
	// TODO: better description (like the ones in synch.c)
	// TODO: implementation
}

/* Signifies that the thread has successfully acquired the lock. The lock cannot
 * already be acquired by any other thread.
 */
void donation_thread_acquire(struct thread *thread, struct lock *lock)
{
	// TODO: better description (like the ones in synch.c)
	// TODO: implementation
}

/* Signifies that the thread holding the lock has released it. The thread
 * holding the lock cannot be blocked by any other lock. The lock must be
 * already held by some thread.
 */
void donation_thread_release(struct lock *lock)
{
	// TODO: better description (like the ones in synch.c)
	// TODO: implementation
}

void donation_set_base_priority(struct thread *thread, int base_priority)
{
	// TODO: better description (like the ones in synch.c)
	// TODO: implementation
}

int donation_get_base_priority(const struct thread *thread)
{
	// TODO: better description (like the ones in synch.c)
	// TODO: implementation
}

/* Initializes the advanced scheduling calculator */
void mlfqs_init(void)
{
/* Sets the base priority of the thread. Can only be called on a thread that is
 * not blocked by anything.
 */
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
