#ifndef __LIB_KERNEL_PRIORITY_QUEUE_H
#define __LIB_KERNEL_PRIORITY_QUEUE_H

/* Priority Queue
 *
 * This is a standard heap-based priority queue.  If the queue needs to grow, it
 * performs so by doubling the size of the allocated array, resulting in an
 * amortized time complexity of O(log n) for all operations, and size complexity
 * of O(n) where n is the number of entries.
 *
 * The pqueue does not use dynamic allocation.  Instead, each
 * structure that can potentially be in a pqueue must embed a
 * struct pqueue_elem member.  All of the pqueue functions operate on
 * these `struct pqueue_elem's.  The pqueue_entry macro allows
 * conversion from a struct pqueue_elem back to a structure object
 * that contains it.  This is the same technique used in the
 * linked list implementation.  Refer to lib/kernel/list.h for a
 * detailed explanation.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Compares the value of two priority queue elements A and B, given
 * auxiliary data AUX.  Returns true if A is less than B, or
 * false if A is greater than or equal to B.
 */
typedef bool pqueue_less_func(const void *a, const void *b, void *aux);

/* Initial storage size of the priority queue. */
#define PQUEUE_INIT_SIZE 4

/* Priority queue element. */
struct pqueue_elem {
	size_t index; /* Index in the priority queue data array */
};

/* Converts pointer to pqueue element PQUEUE_ELEM into a pointer to
 * the structure that PQUEUE_ELEM is embedded inside.  Supply the
 * name of the outer structure STRUCT and the member name MEMBER
 * of the pqueue element.
 */
#define pqueue_entry(PQUEUE_ELEM, STRUCT, MEMBER)                              \
	((STRUCT *)((uint8_t *)&(PQUEUE_ELEM)->index -                               \
							offsetof(STRUCT, MEMBER.index)))

/* Priority queue */
struct pqueue {
	size_t data_size; /* The actual size of the allocated data */
	size_t size; /* The number of the elements in the queue */
	struct pqueue_elem **data; /* The array of pointers to the items stored */
	pqueue_less_func *less; /* Comparison function */
	void *aux; /* Auxilary data for `less' */
};

/* Basic life cycle */
bool pqueue_init(struct pqueue *pq, pqueue_less_func *less, void *aux);
inline void pqueue_elem_init(struct pqueue_elem *elem);
void pqueue_destroy(struct pqueue *pq);

/* Reserving data sizes, to allow for some optimisations by users. */
bool pqueue_reserve(struct pqueue *pq, size_t size);

/* Basic operations */
size_t pqueue_size(struct pqueue *pq);
bool pqueue_push(struct pqueue *pq, struct pqueue_elem *elem);
struct pqueue_elem *pqueue_top(struct pqueue *pq);
struct pqueue_elem *pqueue_pop(struct pqueue *pq);
void pqueue_remove(struct pqueue *pq, struct pqueue_elem *elem);

/* Definition of pqueue_elem_init, as it is pretty lightweight. Since it is
 * inline, it won't be propagated as a symbol and therefore there will be no
 * symbol collisions with other files including this header.
 *
 * Initializes the priority queue elem ELEM to not be in any priority queue
 */
inline void pqueue_elem_init(struct pqueue_elem *elem)
{
	elem->index = 0;
}

#endif /* lib/kernel/priority_queue.h */
