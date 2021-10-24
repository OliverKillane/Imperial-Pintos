/* Priority queue
 *
 * See priority_queue.h for basic information.
 */

#include "priority_queue.h"
#include "threads/malloc.h"
#include <string.h>
#include <debug.h>
#include <stdio.h>

inline list_less_func pqueue_list_less;

/* High level heap operations */
inline bool heap_push(struct heap *heap, struct pqueue_elem *elem,
											pqueue_less_func *less, void *aux);
inline struct pqueue_elem *heap_top(struct heap *heap);
inline struct pqueue_elem *heap_pop(struct heap *heap, pqueue_less_func *less,
																		void *aux);
inline void heap_update(struct heap *heap, struct pqueue_elem *elem,
												pqueue_less_func *less, void *aux);
inline void heap_remove(struct heap *heap, struct pqueue_elem *elem,
												pqueue_less_func *less, void *aux);

/* Low level heap maintenance */
inline void heap_destroy(struct heap *heap);
static bool heap_grow(struct heap *heap);
inline void heap_sift_up(struct heap *heap, size_t index,
												 pqueue_less_func *less, void *aux);
inline void heap_sift_down(struct heap *heap, size_t index,
													 pqueue_less_func *less, void *aux);
inline void heap_swap_indices(struct heap *heap, size_t index1, size_t index2);

/* Initializes the priority queue PQ to store values on a heap using LESS,
 * given the auxiliary data AUX. If the initialization fails, falls back to
 * a linked list.
 */
void pqueue_init(struct pqueue *pq, pqueue_less_func *less, void *aux)
{
	pq->less = less;
	pq->aux = aux;
	pq->heap.data = malloc(PQUEUE_INIT_SIZE * sizeof(struct pqueue_elem *));
	if (!pq->heap.data) {
		pq->is_fallback = true;
		list_init(&pq->list);
		return;
	}
	pq->is_fallback = false;
	pq->heap.data_size = PQUEUE_INIT_SIZE;
	pq->heap.size = 0;
}

/* Reserves a set size for the heap's array. If the new size is successfully
 * reserved, then returns true, otherwise it returns false. Does nothing and
 * returns true if storage is a linked list.
 */
bool pqueue_reserve(struct pqueue *pq, size_t new_size)
{
	if (pq->is_fallback)
		return true;

	if (new_size > pq->heap.data_size) {
		struct pqueue_elem **new_data =
						realloc(pq->heap.data, new_size * sizeof(void *));
		if (!new_data)
			return false;
		pq->heap.data = new_data;
		pq->heap.data_size = new_size;
	}

	return true;
}

/* Removes all elements from PQ and deallocates the heap's storage if needed */
void pqueue_destroy(struct pqueue *pq)
{
	if (!pq->is_fallback)
		heap_destroy(&pq->heap);
}

/* Returns the number of items in the priority queue */
size_t pqueue_size(struct pqueue *pq)
{
	return pq->is_fallback ? list_size(&pq->list) : pq->heap.size;
}

/* Pushes a new element onto the priority queue. If the heap needs to grow and
 * it fails to, falls back to a linked list.
 */
void pqueue_push(struct pqueue *pq, struct pqueue_elem *elem)
{
	if (!pq->is_fallback) {
		ASSERT(!elem->heap_index);
		if (heap_grow(&pq->heap)) {
			pq->heap.data[pq->heap.size] = elem;
			elem->heap_index = pq->heap.size;
			heap_sift_up(&pq->heap, pq->heap.size, pq->less, pq->aux);
		} else {
			struct heap old_heap = pq->heap;
			pq->is_fallback = true;
			while (old_heap.size) {
				list_insert_ordered(&pq->list,
														&heap_pop(&old_heap, pq->less, pq->aux)->elem,
														pqueue_list_less, pq);
			}
			heap_destroy(&old_heap);
		}
	}

	if (pq->is_fallback)
		list_insert_ordered(&pq->list, &elem->elem, pqueue_list_less, pq);
}

/* Returns the smallest element from the priority queue */
struct pqueue_elem *pqueue_top(struct pqueue *pq)
{
	ASSERT(pqueue_size(pq));
	return pq->is_fallback ?
								 list_entry(list_front(&pq->list), struct pqueue_elem, elem) :
								 pq->heap.data[1];
}

/* Removes the smallest element from the priority queue and returns it */
struct pqueue_elem *pqueue_pop(struct pqueue *pq)
{
	ASSERT(pqueue_size(pq));
	return pq->is_fallback ? list_entry(list_pop_front(&pq->list),
																			struct pqueue_elem, elem) :
													 heap_pop(&pq->heap, pq->less, pq->aux);
}

/* Updates the spot in the priority queue where the ELEM is without
 * removing it, assuming it was changed
 */
void pqueue_update(struct pqueue *pq, struct pqueue_elem *elem)
{
	if (pq->is_fallback) {
		list_remove(&elem->elem);
		list_insert_ordered(&pq->list, &elem->elem, pqueue_list_less, pq);
	} else {
		heap_update(&pq->heap, elem, pq->less, pq->aux);
	}
}

/* Removes the given element from inside the priority queue */
void pqueue_remove(struct pqueue *pq, struct pqueue_elem *elem)
{
	if (pq->is_fallback)
		list_remove(&elem->elem);
	else
		heap_remove(&pq->heap, elem, pq->less, pq->aux);
}

/* Comparison function for the fallback into the list; converts the pqueue_elem
 * comparator to the list_elem comparator
 */
bool pqueue_list_less(const struct list_elem *a, const struct list_elem *b,
											void *pq_raw)
{
	struct pqueue *pq = pq_raw;
	return pq->less(list_entry(a, struct pqueue_elem, elem),
									list_entry(b, struct pqueue_elem, elem), pq->aux);
}

/* Adds a new item into the heap. Returns whether it succeeded in doing so
 * or not
 */
bool heap_push(struct heap *heap, struct pqueue_elem *elem,
							 pqueue_less_func *less, void *aux)
{
	ASSERT(!elem->heap_index);
	if (!heap_grow(heap))
		return false;

	heap->data[heap->size] = elem;
	elem->heap_index = heap->size;
	heap_sift_up(heap, heap->size, less, aux);
	return true;
}

/* Returns the smallest item from the heap */
struct pqueue_elem *heap_top(struct heap *heap)
{
	return heap->data[1];
}

/* Removes the smallest item from the heap and returns it */
struct pqueue_elem *heap_pop(struct heap *heap, pqueue_less_func *less,
														 void *aux)
{
	struct pqueue_elem *out = heap->data[1];
	out->heap_index = 0;

	heap->data[1] = heap->data[heap->size--];
	if (heap->size)
		heap->data[1]->heap_index = 1;
	heap_sift_down(heap, 1, less, aux);
	return out;
}

/* Updates the position of ELEM in the heap without removing it */
void heap_update(struct heap *heap, struct pqueue_elem *elem,
								 pqueue_less_func *less, void *aux)
{
	ASSERT(elem->heap_index);
	ASSERT(elem->heap_index <= heap->size);
	if (elem->heap_index > 1 &&
			less(heap->data[elem->heap_index], heap->data[elem->heap_index / 2], aux))
		heap_sift_up(heap, elem->heap_index, less, aux);
	else
		heap_sift_down(heap, elem->heap_index, less, aux);
}

/* Removes the given item from the heap */
void heap_remove(struct heap *heap, struct pqueue_elem *elem,
								 pqueue_less_func *less, void *aux)
{
	ASSERT(elem->heap_index);
	ASSERT(elem->heap_index <= heap->size);
	size_t index = elem->heap_index;
	elem->heap_index = 0;

	if (index == heap->size--)
		return;
	heap->data[index] = heap->data[heap->size + 1];
	heap->data[index]->heap_index = index;
	heap_update(heap, heap->data[index], less, aux);
}

/* Destroys the underlying storage of the heap */
void heap_destroy(struct heap *heap)
{
	free(heap->data);
}

/* Increases the size of the heap by 1, returns true if successful,
 * otherwise false
 */
bool heap_grow(struct heap *heap)
{
	heap->size++;
	if (heap->size >= heap->data_size) {
		struct pqueue_elem **new_data = realloc(
						heap->data, (heap->data_size * 2) * sizeof(struct pqueue_elem *));
		if (!new_data)
			return false;
		heap->data = new_data;
		heap->data_size *= 2;
	}
	return true;
}

/* Sifts the values up the heap, starting from a given index
 * and making sure they are ordered correctly in the heap
 */
void heap_sift_up(struct heap *heap, size_t index, pqueue_less_func *less,
									void *aux)
{
	struct pqueue_elem *elem = heap->data[index];
	while (index > 1) {
		if (less(elem, heap->data[index / 2], aux)) {
			heap_swap_indices(heap, index, index / 2);
			index /= 2;
		} else {
			break;
		}
	}
}

/* Sifts the values down the heap, starting from a given index
 * and making sure they are ordered correctly in the heap
 */
void heap_sift_down(struct heap *heap, size_t index, pqueue_less_func *less,
										void *aux)
{
	struct pqueue_elem *elem = heap->data[index];
	while (index * 2 <= heap->size) {
		if (less(heap->data[index * 2], elem, aux)) {
			if (index * 2 + 1 <= heap->size &&
					less(heap->data[index * 2 + 1], heap->data[index * 2], aux)) {
				heap_swap_indices(heap, index, index * 2 + 1);
				index = index * 2 + 1;
			} else {
				heap_swap_indices(heap, index, index * 2);
				index = index * 2;
			}
		} else if (index * 2 + 1 <= heap->size &&
							 less(heap->data[index * 2 + 1], heap->data[index], aux)) {
			heap_swap_indices(heap, index, index * 2 + 1);
			index = index * 2 + 1;
		} else {
			break;
		}
	}
}

/* Swaps the 'pqueue_elem's in the given indicies on the heap */
void heap_swap_indices(struct heap *heap, size_t index1, size_t index2)
{
	struct pqueue_elem *p = heap->data[index1];
	heap->data[index1] = heap->data[index2];
	heap->data[index2] = p;
	heap->data[index1]->heap_index = index1;
	heap->data[index2]->heap_index = index2;
}
