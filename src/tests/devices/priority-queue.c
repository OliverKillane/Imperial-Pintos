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
#define NUM_ITEMS 100

_Static_assert(NUM_ITEMS % 2 == 0, "NUM_ITEMS must be even");

struct test_elem {
	struct pqueue_elem elem;
	int value;
};

void test_pqueue(void);
static pqueue_less_func test_pqueue_comparator;
static void shuffle(struct pqueue_elem **, size_t);
static void test_pqueue_push_pop(void);
static void test_pqueue_remove(void);

/* Overall test function for priority queue tests */
void test_pqueue(void)
{
	printf("(priority-queue) Starting priority queue tests\n");
	test_pqueue_push_pop();
	printf("(priority-queue) Finished test_pqueue_push_pop() successfully\n");
	test_pqueue_remove();
	printf("(priority-queue) Finished test_pqueue_remove() successfully\n");
}

/* Comparator for the values in the tests cases */
bool test_pqueue_comparator(const struct pqueue_elem *a_raw,
														const struct pqueue_elem *b_raw, void *aux UNUSED)
{
	const struct test_elem *a = pqueue_entry(a_raw, struct test_elem, elem);
	const struct test_elem *b = pqueue_entry(b_raw, struct test_elem, elem);
	return a->value < b->value;
}

/* Shuffles the CNT elements in ARRAY into random order. */
void shuffle(struct pqueue_elem **array, size_t cnt)
{
	size_t i;

	for (i = 0; i < cnt; i++) {
		size_t j = i + random_ulong() % (cnt - i);
		struct pqueue_elem *t = array[j];
		array[j] = array[i];
		array[i] = t;
	}
}

/* Tests with randomly pushing and popping in the pqueue */
void test_pqueue_push_pop(void)
{
	struct pqueue test_queue;
	pqueue_init(&test_queue, test_pqueue_comparator, NULL);

	struct test_elem values[NUM_ITEMS];
	struct pqueue_elem *shuffled_values[NUM_ITEMS];
	for (int i = 0; i < NUM_ITEMS; i++) {
		pqueue_elem_init(&values[i].elem);
		values[i].value = i;
		shuffled_values[i] = &values[i].elem;
	}

	for (int repeat = 0; repeat < NUM_REPEAT; repeat++) {
		shuffle(shuffled_values, NUM_ITEMS);
		for (int i = 0; i < NUM_ITEMS; i++)
			pqueue_push(&test_queue, shuffled_values[i]);
		for (int i = 0; i < NUM_ITEMS; i++) {
			ASSERT(pqueue_entry(pqueue_top(&test_queue), struct test_elem, elem)
										 ->value == i);
			ASSERT(pqueue_entry(pqueue_pop(&test_queue), struct test_elem, elem)
										 ->value == i);
		}
	}
}

/* Tests with randomly pushing and removing from the pqueue */
void test_pqueue_remove(void)
{
	struct pqueue test_queue;
	pqueue_init(&test_queue, test_pqueue_comparator, NULL);

	struct test_elem values[NUM_ITEMS];
	struct pqueue_elem *shuffled_values[NUM_ITEMS];
	for (int i = 0; i < NUM_ITEMS; i++) {
		pqueue_elem_init(&values[i].elem);
		values[i].value = i;
		shuffled_values[i] = &values[i].elem;
	}

	for (int repeat = 0; repeat < NUM_REPEAT; repeat++) {
		shuffle(shuffled_values, NUM_ITEMS);
		for (int i = 0; i < NUM_ITEMS; i++)
			pqueue_push(&test_queue, shuffled_values[i]);
		for (int i = NUM_ITEMS / 2 - 1; i >= 0; i--)
			pqueue_remove(&test_queue, &values[i].elem);

		for (int i = NUM_ITEMS / 2; i < NUM_ITEMS; i++) {
			ASSERT(pqueue_entry(pqueue_top(&test_queue), struct test_elem, elem)
										 ->value == i);
			ASSERT(pqueue_entry(pqueue_pop(&test_queue), struct test_elem, elem)
										 ->value == i);
		}
	}
}
