#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdbool.h>
#include <stdint.h>
#include <bitmap.h>
#include "vm/frame.h"

typedef int32_t swapid_t;

/* Initializes the swap system */
void swap_init(void);

/* Automatically frees that spot and returns whether the entry is writable */
bool swap_load(void *page, swapid_t id);

/* Free the swap slot and returns whether it was writable or not */
bool swap_free(swapid_t id);

/* Swap access functions - used in frame system. */
void swap_page_evict(void *kpage, uint32_t *pd, void *vpage,
										 struct lock *used_queue_lock);
void swap_page_reset_accessed(uint32_t *pd, void *vpage);
bool swap_page_was_accessed(uint32_t *pd, void *vpage);

#endif
