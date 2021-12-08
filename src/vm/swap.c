#include "vm/swap.h"
#include <stdbool.h>
#include <stdint.h>
#include <bitmap.h>
#include "devices/block.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"

#define BLOCKS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

struct bitmap *is_free_tree; /* An interval tree of whether an entry is used */
struct bitmap *is_writable; /* Whether an entry is brought back writable */

struct lock interval_tree_lock; /* Locks our is_free_tree bitmap */

_Static_assert(PGSIZE % BLOCK_SECTOR_SIZE == 0,
							 "Page size must be divisible by block size");

/* Initializes the swap system. */
void swap_init(void)
{
	struct block *swap_block = block_get_role(BLOCK_SWAP);
	if (!swap_block)
		PANIC("Swap block does not exist.");

	block_sector_t swap_space_size = block_size(swap_block);
	int32_t num_swap_spaces = swap_space_size / BLOCKS_PER_PAGE;
	if (num_swap_spaces > (1 << 29))
		PANIC("Max swap space size handled by the OS is 2TB.");

	int32_t nearest_power_of_two = 1;
	while (nearest_power_of_two < num_swap_spaces)
		nearest_power_of_two *= 2;

	is_free_tree = bitmap_create(nearest_power_of_two * 2);
	is_writable = bitmap_create(num_swap_spaces);

	if (!is_free_tree || !is_writable)
		PANIC("Could not malloc is_free_tree memory or is_writable bitmaps.");

	bitmap_set_multiple(is_free_tree, nearest_power_of_two, num_swap_spaces,
											true);

	for (int i = nearest_power_of_two - 1; i > 0; i--)
		bitmap_set(is_free_tree, i,
							 bitmap_test(is_free_tree, i * 2) ||
											 bitmap_test(is_free_tree, i * 2 + 1));

	lock_init(&interval_tree_lock);
}

/* Finds a free swap slot, sets the pte in the page directory to not present
 * and writes the data from that page into that swap slot.
 */
void swap_page_evict(void *kpage, uint32_t *pd, void *vpage)
{
	ASSERT(pg_ofs(kpage) == 0);

	/* Find the first free swap slot. */
	lock_acquire(&interval_tree_lock);

	int32_t node = 1;
	if (!bitmap_test(is_free_tree, node))
		PANIC("Ran out of swap space.");

	int32_t start_of_leaf_nodes = bitmap_size(is_free_tree) / 2;
	while (node < start_of_leaf_nodes) {
		if (bitmap_test(is_free_tree, node * 2))
			node = node * 2;
		else
			node = node * 2 + 1;
	}

	swapid_t new_swap_id = node - start_of_leaf_nodes;
	bitmap_set(is_free_tree, node, false);
	bitmap_set(is_writable, new_swap_id, pagedir_is_writable(pd, vpage));

	while (node /= 2)
		bitmap_set(is_free_tree, node,
							 bitmap_test(is_free_tree, node * 2) ||
											 bitmap_test(is_free_tree, node * 2 + 1));

	lock_release(&interval_tree_lock);

	/* Set the page to swapped. We are assuming that this will succeed, since,
	 * it is being swapped out, then that means that the pte for it should already
	 * be allocated.
	 */
	if (!pagedir_set_swapped_page(pd, vpage, new_swap_id))
		NOT_REACHED();

	/* Write the page to the allocated swap block */
	for (int i = 0; i < BLOCKS_PER_PAGE; i++)
		block_write(block_get_role(BLOCK_SWAP), new_swap_id * BLOCKS_PER_PAGE + i,
								kpage + (i * BLOCK_SECTOR_SIZE));
}

/* Automatically frees that spot and returns whether the entry is writable */
bool swap_load(void *page, swapid_t id)
{
	ASSERT(pg_ofs(page) == 0);

	/* Load the page back from the swap */
	for (int i = 0; i < BLOCKS_PER_PAGE; i++)
		block_read(block_get_role(BLOCK_SWAP), id * BLOCKS_PER_PAGE + i,
							 page + (i * BLOCK_SECTOR_SIZE));

	/* Set the previously used swap block as usable */
	return swap_free(id);
}

/* Free the swap slot. */
bool swap_free(swapid_t id)
{
	/* Update the interval tree with the info that now this swap block is empty */
	lock_acquire(&interval_tree_lock);

	bool was_writable = bitmap_test(is_writable, id);

	int32_t start_of_leaf_nodes = bitmap_size(is_free_tree) / 2;
	int32_t node = id + start_of_leaf_nodes;
	bitmap_set(is_free_tree, node, true);
	while (node /= 2)
		bitmap_set(is_free_tree, node, true);

	lock_release(&interval_tree_lock);

	return was_writable;
}

/* Return true if the page table entry is true. */
bool swap_page_was_accessed(uint32_t *pd, void *vpage)
{
	return pagedir_is_accessed(pd, vpage);
}

/* Set the page table entry access to false. */
void swap_page_reset_accessed(uint32_t *pd, void *vpage)
{
	pagedir_set_accessed(pd, vpage, false);
}
