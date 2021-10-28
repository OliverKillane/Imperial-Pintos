#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>
#include "threads/donation.h"

/* A counting semaphore. */
struct semaphore {
	unsigned value; /* Current value. */
	struct list waiters; /* List of waiting threads. */
};

void sema_init(struct semaphore *sema, unsigned value);
void sema_down(struct semaphore *sema);
bool sema_try_down(struct semaphore *sema);
void sema_up(struct semaphore *sema);
void sema_self_test(void);

/* Lock. */
struct lock {
	/* Owned by synch.c */
	struct thread *holder; /* Thread holding lock (for debugging). */
	struct semaphore semaphore; /* Binary semaphore controlling access. */

	/* Owned by donation.c */
	int8_t priority; /* Lock's current computed priority */
	struct thread *donee; /* The thread that receives the lock's priority */
	struct list_elem donorelem; /* Used in donee's pqueue of donors */
	struct list donors; /* Threads donating their priority to the lock */
	struct semaphore donors_guard; /* Guard of the list of donors */
};

void lock_init(struct lock *lock);
void lock_acquire(struct lock *lock);
bool lock_try_acquire(struct lock *lock);
void lock_release(struct lock *lock);
bool lock_held_by_current_thread(const struct lock *lock);

/* Condition variable. */
struct condition {
	struct list waiters; /* List of waiting semaphore_elems. */
};

void cond_init(struct condition *cond);
void cond_wait(struct condition *cond, struct lock *lock);
void cond_signal(struct condition *cond, struct lock *lock);
void cond_broadcast(struct condition *cond, struct lock *lock);

/* Optimization barrier.
 *
 * The compiler will not reorder operations across an
 * optimization barrier.  See "Optimization Barriers" in the
 * reference guide for more information.
 */
#define barrier() asm volatile("" : : : "memory")

#endif /* threads/synch.h */
