#include "vector.h"
#include "threads/malloc.h"

/* Initialises a vector with an initial capacity of DEFAULT_CAPACITY. */
bool vector_init(struct vector *vec)
{
	ASSERT(vec);
	vec->contents = malloc(DEFAULT_CAPACITY * sizeof(void *));
	if (!vec->contents)
		return false;

	vec->capacity = DEFAULT_CAPACITY;
	vec->size = 0;
	return true;
}

/* Reserves a set capacity for the vector, if the vector's current capacity is
 * larger, ignores. Returns false for a failed reallocation.
 */
bool vector_reserve(struct vector *vec, size_t capacity)
{
	ASSERT(vec);
	if (capacity > vec->capacity) {
		void **new_contents = realloc(vec->contents, capacity * sizeof(void *));

		if (!new_contents)
			return false;

		vec->contents = new_contents;
		vec->capacity = capacity;
	}

	return true;
}

/* Removes all items in the vector and frees memeory containing the vector's
 *contents.
 */
void vector_destroy(struct vector *vec)
{
	ASSERT(vec);
	vec->capacity = 0;
	vec->size = 0;
	free(vec->contents);
}

/* Returns a pointer to the start of the vector's contents. */
void **vector_begin(struct vector *vec)
{
	ASSERT(vec);
	return vec->contents;
}

/* Returns a pointer to the end of the contents (after last item). */
void **vector_end(struct vector *vec)
{
	ASSERT(vec);
	return vec->contents + vec->size;
}

/* Adds a new item to the end of the vector. */
bool vector_push_back(struct vector *vec, void *item)
{
	ASSERT(vec);
	if (vec->size <= vec->capacity && !vector_reserve(vec, vec->capacity * 2))
		return false;
	vec->contents[vec->size] = item;
	vec->size++;
	return true;
}

/* Removes the last element of the vector, returning it. */
void *vector_pop_back(struct vector *vec)
{
	ASSERT(vec);
	ASSERT(vec->size > 0);
	return vec->contents[--vec->size];
}

void *vector_get(struct vector *vec, size_t index)
{
	ASSERT(vec);
	ASSERT(index < vec->size);
	return vec->contents[index];
}

void vector_set(struct vector *vec, size_t index, void *item)
{
	ASSERT(vec);
	ASSERT(index < vec->size);
	vec->contents[index] = item;
}

/* Returns the number of items in the vector. */
size_t vector_size(struct vector *vec)
{
	ASSERT(vec);
	return vec->size;
}

/* Returns true if the vector is empty. */
bool vector_empty(struct vector *vec)
{
	ASSERT(vec);
	return vec->size == 0;
}
