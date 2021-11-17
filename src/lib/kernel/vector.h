#ifndef __LIB_KERNEL_VECTOR_H
#define __LIB_KERNEL_VECTOR_H

/* A vector data type.
 *
 * Takes pointer to items and stores them in an array, resizing the array
 * appropriately when there are more items than capacity, or when the capacity
 * is more than twice the number of items.
 */
#include <stdbool.h>
#include <stddef.h>

struct vector {
	size_t capacity;
	size_t size;
	void **contents;
};

#define DEFAULT_CAPACITY 4

_Static_assert(DEFAULT_CAPACITY > 0, "DEFAULT_CAPACITY has to be positive");

#define VECTOR_FOR(vec, item)                                                  \
	for (void **item = vector_begin(vec); item != vector_end(vec); item++)

bool vector_init(struct vector *vec);
bool vector_reserve(struct vector *vec, size_t capacity);
void vector_destroy(struct vector *vec);

void **vector_begin(struct vector *vec);
void **vector_end(struct vector *vec);

bool vector_push_back(struct vector *vec, void *item);
void *vector_pop_back(struct vector *vec);
void *vector_get(struct vector *vec, size_t index);
void vector_set(struct vector *vec, size_t index, void *item);

size_t vector_size(struct vector *vec);
bool vector_empty(struct vector *vec);

#endif
