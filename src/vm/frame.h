
#ifndef VM_FRAME_H
#define VM_FRAME_H

#ifndef USERPROG
#error "VM requires USERPROG to be enabled as well"
#endif

#include <stdbool.h>
#include <list.h>
#include <stdint.h>
#include "vm/mmap.h"
#include "vm/swap.h"

void frame_init(void);

/* Get a pointer to a a new locked frame. */
void *frame_get(void);

/* Lock a frame to prevent page replacement accessing it. For a fill
 * explanation see 'frame.c'.
 */
bool frame_lock_mmaped(struct shared_mmap *shared_mmap, void *kpage);
bool frame_lock_swappable(uint32_t *pd, void *vpage, void *kpage);

/* Unlock a frame so it can potentially be evicted. */
void frame_unlock_mmaped(struct shared_mmap *shared_mmap, void *kpage);
void frame_unlock_swappable(uint32_t *pd, void *vpage, void *kpage);

/* Free a locked frame */
void frame_free(void *kpage);

#endif
