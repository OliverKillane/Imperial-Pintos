#ifndef THREADS_PALLOC_H
#define THREADS_PALLOC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How to allocate pages.
 *
 * The defines are for preventing the programmer from misusing it with frame.h.
 */
#ifndef VM
enum palloc_flags {
	PAL_ASSERT = 0x01, /* Panic on failure. */
	PAL_ZERO = 0x02, /* Zero page contents. */
	PAL_USER = 0x04 /* User page. */
};
#else
enum palloc_flags {
	PAL_ASSERT = 0x01, /* Panic on failure. */
	PAL_ZERO = 0x02, /* Zero page contents. */
};

#ifdef PAL_USER_ENABLE
#define PAL_USER 0x04 /* User page. */
#endif
#endif

uint32_t palloc_pool_count_free(bool user_pool);

void palloc_init(size_t user_page_limit);
size_t palloc_pool_size(bool user_pool);
uint8_t *palloc_pool_base(bool user_pool);
void *palloc_get_page(enum palloc_flags);
void *palloc_get_multiple(enum palloc_flags, size_t page_cnt);
void palloc_free_page(void *page);
void palloc_free_multiple(void *pages, size_t page_cnt);

#endif /* threads/palloc.h */
