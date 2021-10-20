/* Priority queue
 *
 * See priority_queue.h for basic information.
 */

#include "priority_queue.h"
#include "threads/malloc.h"
#include <string.h>
#include <debug.h>
#include <stdio.h>

inline void pqueue_sift_up(struct pqueue *pq, size_t index);
inline void pqueue_sift_down(struct pqueue *pq, size_t index);
static bool pqueue_grow(struct pqueue *pq);

/* Initializes the priority queue PQ to store values on a heap using LESS,
 * given the auxiliary data AUX, fi the initialisation fails
 */
bool pqueue_init(struct pqueue *pq, pqueue_less_func *less, void *aux)
{
	pq->data_size = PQUEUE_INIT_SIZE;
	pq->size = 0;
	pq->data = malloc(PQUEUE_INIT_SIZE);

	if (!pq->data)
		return false;

	pq->less = less;
	pq->aux = aux;

	return true;
}

/* Reserves a set size for the heap's array. If the new size is successfully
 * reserved, then returns true, otherwise it returns false.
 */
bool pqueue_reserve(struct pqueue *pq, size_t new_size)
{
	if (new_size > pq->data_size) {
		struct pqueue_elem **new_data =
						realloc(pq->data, new_size * sizeof(void *));
		if (!new_data)
			return false;
		pq->data = new_data;
		pq->data_size = new_size;
	}

	return true;
}

/* Removes all elements from PQ and deallocates its storage */
void pqueue_destroy(struct pqueue *pq)
{
	free(pq->data);
}

/* Returns the number of items in the priority queue */
size_t pqueue_size(struct pqueue *pq)
{
	return pq->size;
}

/* Pushes a new element onto the priority queue, returns true if successful,
 * otherwise false
 */
bool pqueue_push(struct pqueue *pq, struct pqueue_elem *elem)
{
	ASSERT(!elem->index);
	if (!pqueue_grow(pq))
		return false;

	pq->data[pq->size] = elem;
	elem->index = pq->size;
	pqueue_sift_up(pq, pq->size);
	return true;
}

/* Returns the smallest element from the priority queue */
struct pqueue_elem *pqueue_top(struct pqueue *pq)
{
	ASSERT(pq->size);
	return pq->data[1];
}

/* Removes the smallest element from the priority queue */
struct pqueue_elem *pqueue_pop(struct pqueue *pq)
{
	ASSERT(pq->size);
	struct pqueue_elem *out = pq->data[1];
	out->index = 0;

	pq->data[1] = pq->data[pq->size--];
	if (pq->size)
		pq->data[1]->index = 1;
	pqueue_sift_down(pq, 1);
	return out;
}

/* Removes the given element from inside the priority queue */
void pqueue_remove(struct pqueue *pq, struct pqueue_elem *elem)
{
	ASSERT(elem->index);
	ASSERT(elem->index <= pq->size);
	size_t index = elem->index;
	elem->index = 0;

	if (index == pq->size--)
		return;
	pq->data[index] = pq->data[pq->size + 1];
	pq->data[index]->index = index;
	if (index > 1 && pq->less(pq->data[index], pq->data[index / 2], pq->aux))
		pqueue_sift_up(pq, index);
	else
		pqueue_sift_down(pq, index);
}

/* Increases the size of the priority queue by 1, returns true if successful,
 * otherwise false
 */
bool pqueue_grow(struct pqueue *pq)
{
	pq->size++;
	if (pq->size >= pq->data_size) {
		struct pqueue_elem **new_data =
						realloc(pq->data, (pq->data_size * 2) * sizeof(void *));
		if (!new_data)
			return false;
		pq->data = new_data;
		pq->data_size *= 2;
	}
	return true;
}

/* Sifts the values up the priority queue, starting from a given index
 * and making sure they are ordered correctly in the heap
 */
void pqueue_sift_up(struct pqueue *pq, size_t index)
{
	struct pqueue_elem *elem = pq->data[index];
	while (index > 1) {
		if (pq->less(elem, pq->data[index / 2], pq->aux)) {
			pq->data[index] = pq->data[index / 2];
			pq->data[index]->index = index;
			pq->data[index /= 2] = elem;
			pq->data[index]->index = index;
		} else {
			break;
		}
	}
}

/* Sifts the values down the priority queue, starting from a given index
 * and making sure they are ordered correctly in the heap
 */
void pqueue_sift_down(struct pqueue *pq, size_t index)
{
	struct pqueue_elem *elem = pq->data[index];
	while (index * 2 <= pq->size) {
		if (pq->less(pq->data[index * 2], elem, pq->aux)) {
			if (index * 2 + 1 <= pq->size &&
					pq->less(pq->data[index * 2 + 1], pq->data[index * 2], pq->aux)) {
				pq->data[index] = pq->data[index * 2 + 1];
				pq->data[index]->index = index;
				pq->data[index = index * 2 + 1] = elem;
				pq->data[index]->index = index;
			} else {
				pq->data[index] = pq->data[index * 2];
				pq->data[index]->index = index;
				pq->data[index *= 2] = elem;
				pq->data[index]->index = index;
			}
		} else if (index * 2 + 1 <= pq->size &&
							 pq->less(pq->data[index * 2 + 1], pq->data[index], pq->aux)) {
			pq->data[index] = pq->data[index * 2 + 1];
			pq->data[index]->index = index;
			pq->data[index = index * 2 + 1] = elem;
			pq->data[index]->index = index;
		} else {
			break;
		}
	}
}
