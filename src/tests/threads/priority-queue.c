/* Test program for lib/kernel/priority_queue.c.
 *
 * Attempts to test the list functionality that is not
 * sufficiently tested elsewhere in Pintos.
 */

#undef NDEBUG
#include <debug.h>
#include <list.h>
#include <random.h>
#include <priority_queue.h>
#include <stdio.h>

#define NUM_REPEAT 100
#define NUM_ITEMS 200

static pqueue_less_func test_pqueue_comparator;
static void shuffle(int *, size_t);

void test_pqueue(void)
{
	struct pqueue test_queue;
	ASSERT(pqueue_init(&test_queue, test_pqueue_comparator, NULL));

	int values[NUM_ITEMS];
	for (int i = 0; i < NUM_ITEMS; i++)
		values[i] = i;
	printf("Initialized the values\n");

	for (int repeat = 0; repeat < NUM_REPEAT; repeat++) {
		shuffle(values, NUM_ITEMS);
		for (int i = 0; i < NUM_ITEMS; i++)
			ASSERT(pqueue_push(&test_queue, &values[i]));
		for (int i = 0; i < NUM_ITEMS; i++) {
			ASSERT(*((int *)pqueue_top(&test_queue)) == i);
			ASSERT(*((int *)pqueue_pop(&test_queue)) == i);
		}
		printf("Finished %d round%s of tests\n", repeat + 1,
					 repeat == 0 ? "" : "s");
	}
}

bool test_pqueue_comparator(const void *a_raw, const void *b_raw,
														void *aux UNUSED)
{
	const int *a = a_raw;
	const int *b = b_raw;
	return *a < *b;
}

/* Shuffles the CNT elements in ARRAY into random order. */
static void shuffle(int *array, size_t cnt)
{
	size_t i;

	for (i = 0; i < cnt; i++) {
		size_t j = i + random_ulong() % (cnt - i);
		int t = array[j];
		array[j] = array[i];
		array[i] = t;
	}
}
