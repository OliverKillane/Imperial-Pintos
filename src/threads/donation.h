#ifndef THREADS_PRIORITY_H
#define THREADS_PRIORITY_H

#include <stdbool.h>
#include <stdint.h>
#include <list.h>
#include <priority_queue.h>
#include <fixed-point.h>

/* The priority donation system. It uses a link-cut-tree-like forest of nodes
 * with priorities being donated to them, where the edge from a child to a
 * parent denotes one node donating its priority to the other, and the node's
 * priority being calculated as a maximum of its donated priorities and its base
 * priority. The nodes can be either threads or locks; in terms of the latter,
 * the base priority is just the PRI_MIN. The simplification over the link-cut
 * tree can be applied in this case because of 2 properties that hold for this
 * system:
 *  - when a node starts giving out a donation, it can only increase the
 *    priorities of its ancestors
 *  - the only donation edges in the forest that can be cut are the edges
 *    leading directly to the roots of the trees - either locks with a thread
 *    being unlocked for them, or threads releasing a lock
 * Because of that, the algorithm for linking in a new node is just iterating
 * over the nodes up to a certain number of iterations (see DONATION_MAX_DEPTH)
 * and updating their donated priorities, and the one for unlinking a node is
 * just a matter of updating the root of the tree.
 *
 * As far as synchronization goes, the whole structure is protected using
 * hand-over-hand locking of access to nodes. This is possible because the
 * trees are only traversed upwards.
 */

/* Forward declaration of lock and thread */
struct lock;
struct thread;

/* Flag for whether the donation system has been turned on - used for avoiding
 * a circular dependency with malloc, initialization of donation system for the
 * main thread and several functions using locks. Requires that all the locks
 * acquired by the main thread before setting it to true must be released before
 * it gets set to true.
 */
extern bool donation_initialised;

/* Maximum depth to which the donation is updated */
#define DONATION_MAX_DEPTH 16
_Static_assert(DONATION_MAX_DEPTH, "DONATION_MAX_DEPTH must be positive");

/* Priority donation system */
void donation_thread_init(struct thread *thread, int8_t base_priority);
void donation_lock_init(struct lock *lock);
void donation_thread_destroy(struct thread *thread);

void donation_thread_block(struct thread *thread, struct lock *lock);
void donation_thread_unblock(struct thread *thread);

void donation_thread_acquire(struct thread *thread, struct lock *lock);
void donation_thread_release(struct lock *lock);

void donation_set_base_priority(struct thread *thread, int base_priority);
int donation_get_base_priority(const struct thread *thread);

#endif /* threads/donation.h */
