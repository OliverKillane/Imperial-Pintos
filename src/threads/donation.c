#include "threads/donation.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* General helper functions for the priority donation system */
static list_less_func donation_thread_less_func;
static list_less_func donation_lock_less_func;
static inline void donation_thread_update_priority(struct thread *thread);
static inline void donation_lock_update_priority(struct lock *lock);
static inline void donation_thread_update_donation(struct thread *thread);
static inline void donation_lock_update_donation(struct lock *lock);

/* Initializes the values in struct thread_priority inside the thread that
 * correspond to the donation system.
 */
void donation_thread_init(struct thread *thread, int8_t base_priority)
{
	thread->priority = thread->base_priority = base_priority;
	thread->donee = NULL;
	list_init(&thread->donors);
}

/* Initializes the values in struct lock_priority inside the lock that
 * correspond to the donation system.
 */
void donation_lock_init(struct lock *lock)
{
	lock->priority = PRI_MIN;
	lock->donee = NULL;
	list_init(&lock->donors);
}

/* Signifies that the thread is now being blocked by a lock. The thread
 * cannot be blocked by any other lock. The function disables intrupts.
 */
void donation_thread_block(struct thread *thread, struct lock *lock)
{
	enum intr_level old_level;

	old_level = intr_disable();
	ASSERT(!thread->donee);
	thread->donee = lock;

	donation_thread_update_priority(thread);

	list_insert_ordered(&lock->donors, &thread->donorelem,
											donation_thread_less_func, NULL);

	for (int depth = 0; depth < DONATION_MAX_DEPTH && lock->donee; depth++) {
		thread = lock->donee;
		donation_lock_update_priority(lock);
		donation_lock_update_donation(lock);

		if (!thread->donee) {
			donation_thread_update_priority(thread);
			ready_queue_update(thread);
			intr_set_level(old_level);
			return;
		}

		lock = thread->donee;
		old_level = intr_disable();
		donation_thread_update_priority(thread);
		donation_thread_update_donation(thread);
	}
	if (!lock->donee)
		donation_lock_update_priority(lock);
	intr_set_level(old_level);
}

/* Signifies that the thread is no longer being blocked by the thread it was
 * marked as blocked by. The thread must be blocked by another lock already.
 * The lock the thread was blocked by must not be acquired by any thread.
 * The function disables intrupts.
 */
void donation_thread_unblock(struct thread *thread)
{
	enum intr_level old_level;
	old_level = intr_disable();
	;
	ASSERT(thread->donee);
	struct lock *lock = thread->donee;
	ASSERT(!lock->donee);
	thread->donee = NULL;

	list_remove(&thread->donorelem);
	donation_lock_update_priority(lock);
	intr_set_level(old_level);
}

/* Signifies that the thread has successfully acquired the lock. The lock cannot
 * already be acquired by any other thread. The function disables intrupts.
 */
void donation_thread_acquire(struct thread *thread, struct lock *lock)
{
	enum intr_level old_level;
	old_level = intr_disable();

	ASSERT(!lock->donee);
	lock->donee = thread;

	donation_lock_update_priority(lock);

	list_insert_ordered(&thread->donors, &lock->donorelem,
											donation_lock_less_func, NULL);
	donation_thread_update_priority(thread);
	intr_set_level(old_level);
}

/* Signifies that the thread holding the lock has released it. The thread
 * holding the lock cannot be blocked by any other lock. The lock must be
 * already held by some thread. The function disables interrupts.
 */
void donation_thread_release(struct lock *lock)
{
	enum intr_level old_level;

	old_level = intr_disable();
	ASSERT(lock->donee);
	struct thread *thread = lock->donee;
	ASSERT(!thread->donee);
	lock->donee = NULL;

	list_remove(&lock->donorelem);

	donation_thread_update_priority(thread);
	intr_set_level(old_level);
}

/* Sets the base priority of the thread. The thread must not be
 * blocked by any other thread. New base priority must be
 * between PRI_MIN and PRI_MAX. The function disables intrupts.
 */
void donation_set_base_priority(struct thread *thread, int base_priority)
{
	enum intr_level old_level;
	old_level = intr_disable();
	ASSERT(!thread->donee);
	ASSERT(base_priority >= PRI_MIN && base_priority <= PRI_MAX);

	thread->base_priority = base_priority;
	donation_thread_update_priority(thread);
	intr_set_level(old_level);
}

/* Returns the base priority of the thread */
int donation_get_base_priority(const struct thread *thread)
{
	return thread->base_priority;
}

/* Comparison function for donor threads stored in the donee lock */
bool donation_thread_less_func(const struct list_elem *a,
															 const struct list_elem *b, void *aux UNUSED)
{
	return list_entry(a, struct thread, donorelem)->priority >
				 list_entry(b, struct thread, donorelem)->priority;
}

/* Comparison function for the donor locks stored in the donee thread */
bool donation_lock_less_func(const struct list_elem *a,
														 const struct list_elem *b, void *aux UNUSED)
{
	return list_entry(a, struct lock, donorelem)->priority >
				 list_entry(b, struct lock, donorelem)->priority;
}

/* Updates the calculated priority of the thread based on the donors and the
 * base priority
 */
inline void donation_thread_update_priority(struct thread *thread)
{
	int8_t new_priority;
	if (list_empty(&thread->donors)) {
		new_priority = thread->base_priority;
	} else {
		new_priority =
						list_entry(list_front(&thread->donors), struct lock, donorelem)
										->priority;
		if (thread->base_priority > new_priority)
			new_priority = thread->base_priority;
	}

	thread->priority = new_priority;
}

/* Updates the calculated priority of the lock based on the donors */
inline void donation_lock_update_priority(struct lock *lock)
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
inline void donation_thread_update_donation(struct thread *thread)
{
	list_remove(&thread->donorelem);
	list_insert_ordered(&thread->donee->donors, &thread->donorelem,
											donation_thread_less_func, NULL);
}

/* Updates the value of the donation given to donors of LOCK */
inline void donation_lock_update_donation(struct lock *lock)
{
	list_remove(&lock->donorelem);
	list_insert_ordered(&lock->donee->donors, &lock->donorelem,
											donation_lock_less_func, NULL);
}
