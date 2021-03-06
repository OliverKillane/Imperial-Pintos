#include "bitmap.h"
#include <debug.h>
#include <limits.h>
#include <round.h>
#include <stdio.h>
#include "threads/malloc.h"
#ifdef FILESYS
#include "filesys/file.h"
#endif

/* Element type.
 *
 * This must be an unsigned integer type at least as wide as int.
 *
 * Each bit represents one bit in the bitmap.
 * If bit 0 in an element represents bit K in the bitmap,
 * then bit 1 in the element represents bit K+1 in the bitmap,
 * and so on.
 */
typedef unsigned long elem_type;

/* Number of bits in an element. */
#define ELEM_BITS (sizeof(elem_type) * CHAR_BIT)

/* Minimum number of elements that are going to be allocated */
#define MIN_ALLOC_SIZE 1

/* From the outside, a bitmap is an array of bits.  From the
 * inside, it's an array of elem_type (defined above) that
 * simulates an array of bits.
 */
struct bitmap {
	size_t elem_alloc_cnt; /* Number of elems allocated in bitfield. */
	size_t bit_cnt; /* Number of bits. */
	elem_type *bits; /* Elements that represent bits. */
};

static inline size_t elem_idx(size_t bit_idx);
static inline elem_type bit_mask(size_t bit_idx);
static inline size_t elem_cnt(size_t bit_cnt);
static inline size_t byte_cnt(size_t bit_cnt);
static inline elem_type last_mask(const struct bitmap *b);

/* Resizes the bitmap. Returns if resize fails. */
bool bitmap_resize(struct bitmap *b, size_t new_size)
{
	const size_t num_elems = elem_cnt(new_size);
	const size_t old_size = b->bit_cnt;

	if (num_elems > b->elem_alloc_cnt) {
		elem_type *const new_bits = realloc(b->bits, num_elems * sizeof(elem_type));
		if (!new_bits)
			return false;
		b->bits = new_bits;
		b->elem_alloc_cnt = num_elems;
	}

	if (new_size > old_size)
		bitmap_set_multiple(b, old_size, new_size - old_size, false);

	b->bit_cnt = new_size;
	return true;
}

/* Add another bit to the end of the bitmap. */
bool bitmap_push_back(struct bitmap *b, bool bit)
{
	if (b->elem_alloc_cnt < elem_cnt(b->bit_cnt + 1)) {
		elem_type *const new_bits = realloc(b->bits, b->elem_alloc_cnt * 2);
		if (!new_bits)
			return false;
		b->bits = new_bits;
		b->elem_alloc_cnt *= 2;
	}

	b->bit_cnt++;
	bitmap_set(b, b->bit_cnt - 1, bit);
	return true;
}

/* Returns the index of the element that contains the bit
 * numbered BIT_IDX.
 */
static inline size_t elem_idx(size_t bit_idx)
{
	return bit_idx / ELEM_BITS;
}

/* Returns an elem_type where only the bit corresponding to
 * BIT_IDX is turned on.
 */
static inline elem_type bit_mask(size_t bit_idx)
{
	return (elem_type)1 << (bit_idx % ELEM_BITS);
}

/* Returns the number of elements required for BIT_CNT bits. */
static inline size_t elem_cnt(size_t bit_cnt)
{
	return DIV_ROUND_UP(bit_cnt, ELEM_BITS);
}

/* Returns the number of bytes required for BIT_CNT bits. */
static inline size_t byte_cnt(size_t bit_cnt)
{
	return sizeof(elem_type) * elem_cnt(bit_cnt);
}

/* Returns a bit mask in which the bits actually used in the last
 * element of B's bits are set to 1 and the rest are set to 0.
 */
static inline elem_type last_mask(const struct bitmap *b)
{
	int last_bits = b->bit_cnt % ELEM_BITS;
	return last_bits ? ((elem_type)1 << last_bits) - 1 : (elem_type)-1;
}

/* Creation and destruction. */

/* Initializes B to be a bitmap of BIT_CNT bits
 * and sets all of its bits to false.
 * Returns true if success, false if memory allocation
 * failed.
 */
struct bitmap *bitmap_create(size_t bit_cnt)
{
	struct bitmap *b = malloc(sizeof *b);
	if (b) {
		b->bit_cnt = bit_cnt;
		b->elem_alloc_cnt = elem_cnt(bit_cnt);
		if (b->elem_alloc_cnt < MIN_ALLOC_SIZE)
			b->elem_alloc_cnt = MIN_ALLOC_SIZE;
		b->bits = malloc(b->elem_alloc_cnt * sizeof(elem_type));
		if (b->bits) {
			bitmap_set_all(b, false);
			return b;
		}
		free(b);
	}
	return NULL;
}

/* Creates and returns a bitmap with BIT_CNT bits in the
 * BLOCK_SIZE bytes of storage preallocated at BLOCK.
 * BLOCK_SIZE must be at least bitmap_needed_bytes(BIT_CNT).
 */
struct bitmap *bitmap_create_in_buf(size_t bit_cnt, void *block,
																		size_t block_size UNUSED)
{
	struct bitmap *b = block;

	ASSERT(block_size >= bitmap_buf_size(bit_cnt));

	b->bit_cnt = bit_cnt;
	b->bits = (elem_type *)(b + 1);
	bitmap_set_all(b, false);
	return b;
}

/* Returns the number of bytes required to accomodate a bitmap
 * with BIT_CNT bits (for use with bitmap_create_in_buf()).
 */
size_t bitmap_buf_size(size_t bit_cnt)
{
	return sizeof(struct bitmap) + byte_cnt(bit_cnt);
}

/* Destroys bitmap B, freeing its storage.
 * Not for use on bitmaps created by
 * bitmap_create_preallocated().
 */
void bitmap_destroy(struct bitmap *b)
{
	if (b) {
		free(b->bits);
		free(b);
	}
}

/* Bitmap size. */

/* Returns the number of bits in B. */
size_t bitmap_size(const struct bitmap *b)
{
	return b->bit_cnt;
}

/* Setting and testing single bits. */

/* Atomically sets the bit numbered IDX in B to VALUE. */
void bitmap_set(struct bitmap *b, size_t idx, bool value)
{
	ASSERT(b);
	ASSERT(idx < b->bit_cnt);
	if (value)
		bitmap_mark(b, idx);
	else
		bitmap_reset(b, idx);
}

/* Atomically sets the bit numbered BIT_IDX in B to true. */
void bitmap_mark(struct bitmap *b, size_t bit_idx)
{
	size_t idx = elem_idx(bit_idx);
	elem_type mask = bit_mask(bit_idx);

	/* This is equivalent to `b->bits[idx] |= mask' except that it
	 * is guaranteed to be atomic on a uniprocessor machine.  See
	 * the description of the OR instruction in [IA32-v2b].
	 */
	asm("orl %1, %0" : "=m"(b->bits[idx]) : "r"(mask) : "cc");
}

/* Atomically sets the bit numbered BIT_IDX in B to false. */
void bitmap_reset(struct bitmap *b, size_t bit_idx)
{
	size_t idx = elem_idx(bit_idx);
	elem_type mask = bit_mask(bit_idx);

	/* This is equivalent to `b->bits[idx] &= ~mask' except that it
	 * is guaranteed to be atomic on a uniprocessor machine.  See
	 * the description of the AND instruction in [IA32-v2a].
	 */
	asm("andl %1, %0" : "=m"(b->bits[idx]) : "r"(~mask) : "cc");
}

/* Atomically toggles the bit numbered IDX in B;
 * that is, if it is true, makes it false,
 * and if it is false, makes it true.
 */
void bitmap_flip(struct bitmap *b, size_t bit_idx)
{
	size_t idx = elem_idx(bit_idx);
	elem_type mask = bit_mask(bit_idx);

	/* This is equivalent to `b->bits[idx] ^= mask' except that it
	 * is guaranteed to be atomic on a uniprocessor machine.  See
	 * the description of the XOR instruction in [IA32-v2b].
	 */
	asm("xorl %1, %0" : "=m"(b->bits[idx]) : "r"(mask) : "cc");
}

/* Returns the value of the bit numbered IDX in B. */
bool bitmap_test(const struct bitmap *b, size_t idx)
{
	ASSERT(b);
	ASSERT(idx < b->bit_cnt);
	return (b->bits[elem_idx(idx)] & bit_mask(idx)) != 0;
}

/* Setting and testing multiple bits. */

/* Sets all bits in B to VALUE. */
void bitmap_set_all(struct bitmap *b, bool value)
{
	ASSERT(b);

	bitmap_set_multiple(b, 0, bitmap_size(b), value);
}

/* Sets the CNT bits starting at START in B to VALUE. */
void bitmap_set_multiple(struct bitmap *b, size_t start, size_t cnt, bool value)
{
	size_t i;

	ASSERT(b);
	ASSERT(start <= b->bit_cnt);
	ASSERT(start + cnt <= b->bit_cnt);

	for (i = 0; i < cnt; i++)
		bitmap_set(b, start + i, value);
}

/* Returns the number of bits in B between START and START + CNT,
 * exclusive, that are set to VALUE.
 */
size_t bitmap_count(const struct bitmap *b, size_t start, size_t cnt,
										bool value)
{
	size_t i, value_cnt;

	ASSERT(b);
	ASSERT(start <= b->bit_cnt);
	ASSERT(start + cnt <= b->bit_cnt);

	value_cnt = 0;
	for (i = 0; i < cnt; i++)
		if (bitmap_test(b, start + i) == value)
			value_cnt++;
	return value_cnt;
}

/* Returns true if any bits in B between START and START + CNT,
 * exclusive, are set to VALUE, and false otherwise.
 */
bool bitmap_contains(const struct bitmap *b, size_t start, size_t cnt,
										 bool value)
{
	size_t i;

	ASSERT(b);
	ASSERT(start <= b->bit_cnt);
	ASSERT(start + cnt <= b->bit_cnt);

	for (i = 0; i < cnt; i++)
		if (bitmap_test(b, start + i) == value)
			return true;
	return false;
}

/* Returns true if any bits in B between START and START + CNT,
 * exclusive, are set to true, and false otherwise.
 */
bool bitmap_any(const struct bitmap *b, size_t start, size_t cnt)
{
	return bitmap_contains(b, start, cnt, true);
}

/* Returns true if no bits in B between START and START + CNT,
 * exclusive, are set to true, and false otherwise.
 */
bool bitmap_none(const struct bitmap *b, size_t start, size_t cnt)
{
	return !bitmap_contains(b, start, cnt, true);
}

/* Returns true if every bit in B between START and START + CNT,
 * exclusive, is set to true, and false otherwise.
 */
bool bitmap_all(const struct bitmap *b, size_t start, size_t cnt)
{
	return !bitmap_contains(b, start, cnt, false);
}

/* Finding set or unset bits. */

/* Finds and returns the starting index of the first group of CNT
 * consecutive bits in B at or after START that are all set to
 * VALUE.
 * If there is no such group, returns BITMAP_ERROR.
 */
size_t bitmap_scan(const struct bitmap *b, size_t start, size_t cnt, bool value)
{
	ASSERT(b);
	ASSERT(start <= b->bit_cnt);

	if (cnt <= b->bit_cnt) {
		size_t last = b->bit_cnt - cnt;
		size_t i;
		for (i = start; i <= last; i++)
			if (!bitmap_contains(b, i, cnt, !value))
				return i;
	}
	return BITMAP_ERROR;
}

/* Finds the first group of CNT consecutive bits in B at or after
 * START that are all set to VALUE, flips them all to !VALUE,
 * and returns the index of the first bit in the group.
 * If there is no such group, returns BITMAP_ERROR.
 * If CNT is zero, returns 0.
 * Bits are set atomically, but testing bits is not atomic with
 * setting them.
 */
size_t bitmap_scan_and_flip(struct bitmap *b, size_t start, size_t cnt,
														bool value)
{
	size_t idx = bitmap_scan(b, start, cnt, value);
	if (idx != BITMAP_ERROR)
		bitmap_set_multiple(b, idx, cnt, !value);
	return idx;
}

/* Finds the index of the first bit set to value. If none are, returns
 * BITMAP_ERROR.
 */
size_t bitmap_first(const struct bitmap *b, bool value)
{
	ASSERT(b);
	for (size_t i = 0; i < b->bit_cnt; i++) {
		if (bitmap_test(b, i) == value)
			return i;
	}
	return BITMAP_ERROR;
}

/* File input and output. */

#ifdef FILESYS
/* Returns the number of bytes needed to store B in a file. */
size_t bitmap_file_size(const struct bitmap *b)
{
	return byte_cnt(b->bit_cnt);
}

/* Reads B from FILE.  Returns true if successful, false
 * otherwise.
 */
bool bitmap_read(struct bitmap *b, struct file *file)
{
	bool success = true;
	if (b->bit_cnt > 0) {
		off_t size = byte_cnt(b->bit_cnt);
		success = file_read_at(file, b->bits, size, 0) == size;
		b->bits[elem_cnt(b->bit_cnt) - 1] &= last_mask(b);
	}
	return success;
}

/* Writes B to FILE.  Return true if successful, false
 * otherwise.
 */
bool bitmap_write(const struct bitmap *b, struct file *file)
{
	off_t size = byte_cnt(b->bit_cnt);
	return file_write_at(file, b->bits, size, 0) == size;
}
#endif /* FILESYS */

/* Debugging. */

/* Dumps the contents of B to the console as hexadecimal. */
void bitmap_dump(const struct bitmap *b)
{
	hex_dump(0, b->bits, byte_cnt(b->bit_cnt), false);
}
