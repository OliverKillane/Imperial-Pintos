#ifndef USERPROG_PAGEDIR_H
#define USERPROG_PAGEDIR_H

#include <stdbool.h>
#include <stdint.h>
#ifdef VM
#include "threads/pte.h"
#include "vm/lazy.h"
#include "vm/mmap.h"
#include "vm/swap.h"
#endif

uint32_t *pagedir_create(void);
void pagedir_destroy(uint32_t *pd);
bool pagedir_set_page(uint32_t *pd, void *upage, void *kpage, bool rw);
void *pagedir_get_page(uint32_t *pd, const void *uaddr);
void pagedir_clear_page(uint32_t *pd, void *upage);
bool pagedir_is_dirty(uint32_t *pd, const void *upage);
void pagedir_set_dirty(uint32_t *pd, const void *upage, bool dirty);
bool pagedir_is_accessed(uint32_t *pd, const void *upage);
void pagedir_set_accessed(uint32_t *pd, const void *upage, bool accessed);
bool pagedir_is_writable(uint32_t *pd, const void *upage);
void pagedir_activate(uint32_t *pd);

#ifdef VM

bool pagedir_set_zeroed_page(uint32_t *pd, void *vpage, bool writable,
														 uint32_t aux);
bool pagedir_set_swapped_page(uint32_t *pd, void *vpage, swapid_t swapid);
bool pagedir_set_mmaped_page(uint32_t *pd, void *vpage,
														 struct user_mmap *mmaped_page);
bool pagedir_set_lazy_page(uint32_t *pd, void *vpage,
													 struct lazy_load *lazy_page);
bool pagedir_is_zeroed_writable(uint32_t *pd, const void *vpage);
uint32_t pagedir_get_zeroed_aux(uint32_t *pd, const void *vpage);
enum page_type pagedir_get_page_type(uint32_t *pd, const void *vpage);
uint32_t pagedir_get_raw_pte(uint32_t *pd, const void *vpage);

#endif

#endif /* userprog/pagedir.h */
