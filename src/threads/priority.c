#include "threads/priority.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* General helper functions for the priority donation system */
static list_less_func donation_thread_less_func;
static pqueue_less_func donation_lock_less_func;
static inline void donation_cascading_update(struct thread *thread,
																						 struct lock *lock,
																						 bool is_curr_thread);
static inline void donation_thread_update_donee(struct thread *thread);
static inline void donation_lock_update_donee(struct lock *lock);
static inline void donation_thread_update_donor(struct thread *thread);
static inline void donation_lock_update_donor(struct lock *lock);

/* Initializes the values in struct thread_priority inside the thread that
 * correspond to the donation system, as well as the semaphore-based guard for
 * that data
 */
void donation_thread_init(struct thread *thread, int8_t base_priority)
{
	thread->priority = thread->base_priority = base_priority;
	sema_init(&thread->priority_guard, 1);
	thread->donee = NULL;
	pqueue_init(&thread->donors, donation_lock_less_func, NULL);
}

/* Initializes the values in struct lock_priority inside the lock that
 * correspond to the donation system, as well as the semaphore-based guard for
 * that data
 */
void donation_lock_init(struct lock *lock)
{
	sema_init(&lock->priority_guard, 1);
	lock->priority = PRI_MIN;
	lock->donee = NULL;
	pqueue_elem_init(&lock->donorelem);
	list_init(&lock->donors);
}

/* Frees the resources of the thread that are related to priority donation */
void donation_thread_destroy(struct thread *thread)
{
	ASSERT(intr_get_level() == INTR_OFF);
	pqueue_destroy(&thread->donors);
}

/* Signifies that the thread is now being blocked by a lock. The thread
 * cannot be blocked by any other lock. The function blocks the access to the
 * data of both thread and the lock.
 */
void donation_thread_block(struct thread *thread, struct lock *lock)
{
	sema_down(&thread->priority_guard);
	sema_down(&lock->priority_guard);

	ASSERT(!thread->donee);
	thread->donee = lock;
	list_insert_ordered(&lock->donors, &thread->donorelem,
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
	ASSERT(thread->donee);

	struct lock *lock = thread->donee;
	sema_down(&lock->priority_guard);
	ASSERT(!lock->donee);

	list_remove(&thread->donorelem);
	thread->donee = NULL;
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
	ASSERT(!lock->donee);
	lock->donee = thread;
	pqueue_push(&lock->donee->donors, &lock->donorelem);
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
	ASSERT(lock->donee);

	struct thread *thread = lock->donee;
	sema_down(&thread->priority_guard);
	ASSERT(!lock->donee->donee);

	pqueue_remove(&thread->donors, &lock->donorelem);
	lock->donee = NULL;
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
	ASSERT(!thread->donee);
	ASSERT(base_priority >= PRI_MIN && base_priority <= PRI_MAX);

	thread->base_priority = base_priority;
	donation_thread_update_donee(thread);

	sema_down(&thread->priority_guard);
}

/* Returns the base priority of the thread */
int donation_get_base_priority(const struct thread *thread)
{
	return thread->base_priority;
}

/* Updates the donated priority of DONATION_MAX_DEPTH threads down the line.
 * It uses hand-over-hand locking to traverse to the subsequent donees. It
 * assumes that the initial donor and its donee already have their guards
 * acquired.
 */
inline void donation_cascading_update(struct thread *thread, struct lock *lock,
																			bool is_curr_thread)
{
	for (int depth = 0;
			 (depth < DONATION_MAX_DEPTH && ((is_curr_thread && thread->donee) ||
																			 (!is_curr_thread && lock->donee)));
			 depth++, is_curr_thread ^= true) {
		if (is_curr_thread) {
			lock = thread->donee;
			if (depth)
				sema_down(&lock->priority_guard);
			donation_thread_update_donor(thread);
			donation_lock_update_donee(lock);
			sema_up(&thread->priority_guard);
		} else {
			thread = lock->donee;
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
	struct thread *a = list_entry(a_raw, struct thread, donorelem);
	struct thread *b = list_entry(b_raw, struct thread, donorelem);
	return a->priority > b->priority;
}

/* Comparison function for the donor locks stored in the donee thread */
bool donation_lock_less_func(const struct pqueue_elem *a_raw,
														 const struct pqueue_elem *b_raw, void *aux UNUSED)
{
	struct lock *a = pqueue_entry(a_raw, struct lock, donorelem);
	struct lock *b = pqueue_entry(b_raw, struct lock, donorelem);
	return a->priority > b->priority;
}

/* Updates the calculated priority of the thread based on the donors and the
 * base priority
 */
inline void donation_thread_update_donee(struct thread *thread)
{
	int8_t new_priority;
	if (pqueue_size(&thread->donors)) {
		new_priority =
						pqueue_entry(pqueue_top(&thread->donors), struct lock, donorelem)
										->priority;
		if (thread->base_priority > new_priority)
			new_priority = thread->base_priority;
	} else {
		new_priority = thread->base_priority;
	}

	thread->priority = new_priority;
	ready_queue_update(thread);
}

/* Updates the calculated priority of the lock based on the donors */
inline void donation_lock_update_donee(struct lock *lock)
{
	if (list_empty(&lock->donors)) {
		lock->priority = PRI_MIN;
	} else {
		lock->priority =
						list_entry(list_front(&lock->donors), struct thread, donorelem)
										->priority;
	}
}

/* Updates the value of the donation given to donors of THREAD */
inline void donation_thread_update_donor(struct thread *thread)
{
	list_remove(&thread->donorelem);
	list_insert_ordered(&thread->donee->donors, &thread->donorelem,
											donation_thread_less_func, NULL);
}

/* Updates the value of the donation given to donors of LOCK */
inline void donation_lock_update_donor(struct lock *lock)
{
	pqueue_update(&lock->donee->donors, &lock->donorelem);
}
