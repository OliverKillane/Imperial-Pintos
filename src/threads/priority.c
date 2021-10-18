// TODO: add internal functions
// TODO: add implementations
// TODO: add template for the file

#include "threads/priority.h"
#include "threads/thread.h"

/* Informs the scheduler that the priority of the thread has changed */
static void tqueue_priority_update(struct thread *thread);
static void mlfqs_init(void);

/* If false (default), use round-robin scheduler.
 * If true, use multi-level feedback queue scheduler.
 * Controlled by kernel command-line option "-mlfqs".
 */
bool thread_mlfqs;

/* Initializes the priority scheduler and its components */
void tqueue_init(void)
{
	// TODO: better description (like the ones in synch.c)
	// TODO: implementation
}

/* Retreives the next thread to be scheduled without popping it from the queue
 */
struct thread *tqueue_front(void)
{
	// TODO: better description (like the ones in synch.c)
	// TODO: implementation
	return NULL;
}

/* Pops the next thread to be scheduled from the queue and returns it */
struct thread *tqueue_next(void)
{
	// TODO: better description (like the ones in synch.c)
	// TODO: implementation
	return NULL;
}

/* Makes the scheduler acknowledge the thread and inherit the priority
 * parameters from the parent thread
 */
void tqueue_thread_init(struct thread *thread, struct thread *parent)
{
	// TODO: better description (like the ones in synch.c)
	// TODO: implementation
}

/* Makes the scheduler acknowledge that the thread has been unblocked */
void tqueue_add(struct thread *thread)
{
	// TODO: better description (like the ones in synch.c)
	// TODO: implementation
}

/* Makes the scheduler acknowledge that the thread has been blocked from
 * the queue
 */
void tqueue_remove(struct thread *thread)
{
	// TODO: better description (like the ones in synch.c)
	// TODO: implementation
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
void mlfqs_get_priority(const struct thread *thread)
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
