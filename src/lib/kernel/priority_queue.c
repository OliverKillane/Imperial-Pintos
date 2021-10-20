/* Priority queue
 *
 * See priority_queue.h for basic information.
 */

#include "priority_queue.h"
#include "threads/malloc.h"
#include <string.h>
#include <debug.h>

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

/* reserves a set size for the heap's array, if the size reserved is smaller
 * than the current size, or the size is updated then true is returned.
 * Else false.
 */
bool pqueue_reserve(struct pqueue *pq, size_t new_size)
{
	if (new_size > pq->data_size) {
		void **new_data = realloc(pq->data, new_size * sizeof(void *));
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

/* Pushes a new item onto the priority queue, returns true if successful,
 * otherwise false
 */
bool pqueue_push(struct pqueue *pq, void *new)
{
	if (!pqueue_grow(pq))
		return false;

	pq->data[pq->size] = new;

	size_t index = pq->size;
	while (index > 1) {
		if (pq->less(new, pq->data[index / 2], pq->aux)) {
			pq->data[index] = pq->data[index / 2];
			pq->data[index /= 2] = new;
		} else {
			break;
		}
	}
	return true;
}

/* Retrieves the smallest item from the priority queue */
void *pqueue_top(struct pqueue *pq)
{
	ASSERT(pq->size);
	return pq->data[1];
}

/* Removes the smallest item from the priority queue */
void *pqueue_pop(struct pqueue *pq)
{
	ASSERT(pq->size);
	void *out = pq->data[1];

	pq->data[1] = pq->data[pq->size--];
	size_t index = 1;
	while (index * 2 <= pq->size) {
		if (pq->less(pq->data[index * 2], pq->data[index], pq->aux)) {
			if (index * 2 + 1 <= pq->size &&
					pq->less(pq->data[index * 2 + 1], pq->data[index * 2], pq->aux)) {
				void *p = pq->data[index];
				pq->data[index] = pq->data[index * 2 + 1];
				pq->data[index = index * 2 + 1] = p;
			} else {
				void *p = pq->data[index];
				pq->data[index] = pq->data[index * 2];
				pq->data[index *= 2] = p;
			}
		} else if (index * 2 + 1 <= pq->size &&
							 pq->less(pq->data[index * 2 + 1], pq->data[index], pq->aux)) {
			void *p = pq->data[index];
			pq->data[index] = pq->data[index * 2 + 1];
			pq->data[index = index * 2 + 1] = p;
		} else {
			break;
		}
	}
	return out;
}

/* Increases the size of the priority queue by 1, returns true if successful,
 * otherwise false
 */
bool pqueue_grow(struct pqueue *pq)
{
	pq->size++;
	if (pq->size >= pq->data_size) {
		void **new_data = realloc(pq->data, (pq->data_size * 2) * sizeof(void *));
		if (!new_data)
			return false;
		pq->data = new_data;
		pq->data_size *= 2;
	}
	return true;
}
