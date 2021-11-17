#ifndef __LIB_KERNEL_BITMAP_H
#define __LIB_KERNEL_BITMAP_H

#include <stdbool.h>
#include <stddef.h>
#include <inttypes.h>

/* Bitmap abstract data type. */

/* Creation and destruction. */
struct bitmap *bitmap_create(size_t bit_cnt);
struct bitmap *bitmap_create_in_buf(size_t bit_cnt, void *block,
																		size_t byte_cnt);
size_t bitmap_buf_size(size_t bit_cnt);
void bitmap_destroy(struct bitmap *b);

/* Resize the bitmap */
bool bitmap_resize(struct bitmap *b, size_t new_size);

/* Add another bit to the end of the bitmap. */
bool bitmap_push_back(struct bitmap *b, bool value);

/* Bitmap size. */
size_t bitmap_size(const struct bitmap *b);

/* Setting and testing single bits. */
void bitmap_set(struct bitmap *b, size_t idx, bool value);
void bitmap_mark(struct bitmap *b, size_t idx);
void bitmap_reset(struct bitmap *b, size_t idx);
void bitmap_flip(struct bitmap *b, size_t idx);
bool bitmap_test(const struct bitmap *b, size_t idx);

/* Setting and testing multiple bits. */
void bitmap_set_all(struct bitmap *b, bool value);
void bitmap_set_multiple(struct bitmap *b, size_t start, size_t cnt,
												 bool value);
size_t bitmap_count(const struct bitmap *b, size_t start, size_t cnt,
										bool value);
bool bitmap_contains(const struct bitmap *b, size_t start, size_t cnt,
										 bool value);
bool bitmap_any(const struct bitmap *b, size_t start, size_t cnt);
bool bitmap_none(const struct bitmap *b, size_t start, size_t cnt);
bool bitmap_all(const struct bitmap *b, size_t start, size_t cnt);

/* Finding set or unset bits. */
#define BITMAP_ERROR SIZE_MAX
size_t bitmap_scan(const struct bitmap *b, size_t start, size_t cnt,
									 bool value);
size_t bitmap_scan_and_flip(struct bitmap *b, size_t start, size_t cnt,
														bool value);
size_t bitmap_first(const struct bitmap *b, bool value);

/* File input and output. */
#ifdef FILESYS
struct file;
size_t bitmap_file_size(const struct bitmap *b);
bool bitmap_read(struct bitmap *b, struct file *file);
bool bitmap_write(const struct bitmap *b, struct file *file);
#endif

/* Debugging. */
void bitmap_dump(const struct bitmap *b);

#endif /* lib/kernel/bitmap.h */
