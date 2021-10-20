#include <list.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/priority.h"
#include "threads/thread.h"
#include "threads/interrupt.h"

/* Array of all ready lists, each ready list at priority (index + PRI_MIN). */
static struct ready_queue ready_queue_array[1 + PRI_MAX - PRI_MIN];

/* List containing all non-empty ready_queues,
 * in ascending order by priority.
 */
static struct list nonempty_ready_queues;

static void tqueue_priority_update(struct thread *thread, int8_t new_priority);
static void mlfqs_init(void);
static bool ready_queue_cmp(const struct list_elem *a,
														const struct list_elem *b, void *aux UNUSED);

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
	for (int index = 0; index <= PRI_MAX - PRI_MIN; index++)
		list_init(&ready_queue_array[index]);
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
	enum intr_level old_level;
	old_level = intr_disable();

	memcpy(&thread->priority, &parent->priority, sizeof(struct thread_priority));
	tqueue_add(thread);

	intr_set_level(old_level);
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

/* Initializes the values in struct thread_priority inside the thread that
 * correspond to the donation system
 */
void donation_thread_init(struct thread *thread)
{
	// TODO: better description (like the ones in synch.c)
	// TODO: implementation
}

/* Initializes the values in struct lcok_priority inside the lock that
 * correspond to the donation system
 */
void donation_lock_init(struct lock *lock)
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

/* Sets the base priority of the thread. May cause the scheduler to update
 * the next thread it wants to run.
 */
void donation_set_base_priority(struct thread *thread)
{
	// TODO: better description (like the onces in synch.c)
	// TODO: implementation
}

/* Returns the base priority of the thread */
int donation_get_base_priority(const struct thread *thread)
{
	// TODO: better description (like the onces in synch.c)
	// TODO: implementation
}

/* Initializes the advanced scheduling calculator */
void mlfqs_init(void)
{
	// TODO: better description (like the ones in synch.c)
	// TODO: implementation
}

/* Sets the base niceness value */
void mlfqs_set_nice(struct thread *thread, int nice)
{
	// TODO: better description (like the ones in synch.c)
	// TODO: implementation
}

/* Gets the base niceness value */
int mlfqs_get_nice(const struct thread *thread)
{
	// TODO: better description (like the ones in synch.c)
	// TODO: implementation
	return 0;
}

/* Increments the recent_cpu of the thread */
void mlfqs_tick(struct thread *thread)
{
	// TODO: better description (like the ones in synch.c)
	// TODO: implementation
}

/* Returns the recent_cpu of the thread */
int mlfqs_get_recent_cpu(const struct thread *thread)
{
	// TODO: better description (like the ones in synch.c)
	// TODO: implementation
	return 0;
}

/* Returns the priority calculated using mlfqs */
int mlfqs_get_priority(const struct thread *thread)
{
	// TODO: better description (like the ones in synch.c)
	// TODO: implementation
}

/* Performs exponential average step on all recent_cpu and load_avg */
void mlfqs_decay(void)
{
	// TODO: better description (like the ones in synch.c)
	// TODO: implementation
}

/* Returns the load average */
int mlfqs_get_load_avg(void)
{
	// TODO: better description (like the ones in synch.c)
	// TODO: implementation
	return 0;
}
