#ifndef VM_MMAP_H
#define VM_MMAP_H

#include <list.h>
#include <stdbool.h>
#include "threads/synch.h"
#include "filesys/file.h"
#include "filesys/off_t.h"

struct user_mmap;
struct shared_mmap;

/* Initializes the mmap system */
void mmap_init(void);

/* Mmap registration. */
bool mmap_register(struct file *file, off_t offset, int16_t length,
									 bool writable, uint32_t *pd, void *upage,
									 struct list *mmaping_list);
void mmap_unregister(struct user_mmap *user_mmap);

/* USER_MMAP access from THREAD's bookkeeping list. */
struct user_mmap *mmap_list_entry(struct list_elem *elem);

/* Load an mmap and set page table entries accordingly. */
void mmap_load(struct user_mmap *user_mmap);

/* Access functions for mmaped pages - used in frame system. */
void mmap_frame_evict(void *kpage, struct shared_mmap *shared_mmap);
bool mmap_frame_was_accessed(struct shared_mmap *shared_mmap);
void mmap_frame_reset_accessed(struct shared_mmap *shared_mmap);

#endif
