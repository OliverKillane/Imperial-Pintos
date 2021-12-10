#ifndef THREADS_PTE_H
#define THREADS_PTE_H

#include "threads/vaddr.h"

#ifdef VM
#include "vm/lazy.h"
#include "vm/mmap.h"
#include "vm/swap.h"
#endif

/* Functions and macros for working with x86 hardware page
 * tables.
 *
 * See vaddr.h for more generic functions and macros for virtual
 * addresses.
 *
 * Virtual addresses are structured as follows:
 *
 * 31                  22 21                  12 11                   0
 * +----------------------+----------------------+----------------------+
 * | Page Directory Index |   Page Table Index   |    Page Offset       |
 * +----------------------+----------------------+----------------------+
 */

/* Page table index (bits 12:21). */
#define PTSHIFT PGBITS /* First page table bit. */
#define PTBITS 10 /* Number of page table bits. */
#define PTSPAN (1 << PTBITS << PGBITS) /* Bytes covered by a page table. */
#define PTMASK BITMASK(PTSHIFT, PTBITS) /* Page table bits (12:21). */

/* Page directory index (bits 22:31). */
#define PDSHIFT (PTSHIFT + PTBITS) /* First page directory bit. */
#define PDBITS 10 /* Number of page dir bits. */
#define PDMASK BITMASK(PDSHIFT, PDBITS) /* Page directory bits (22:31). */

/* Obtains page table index from a virtual address. */
static inline unsigned pt_no(const void *va)
{
	return ((uintptr_t)va & PTMASK) >> PTSHIFT;
}

/* Obtains page directory index from a virtual address. */
static inline uintptr_t pd_no(const void *va)
{
	return (uintptr_t)va >> PDSHIFT;
}

/* Page directory and page table entries.
 *
 * For more information see the section on page tables in the
 * Pintos reference guide chapter, or [IA32-v3a] 3.7.6
 * "Page-Directory and Page-Table Entries".
 *
 * PDEs and PTEs share a common format:
 *
 * 31                                 12 11                     0
 * +------------------------------------+------------------------+
 * |         Physical Address           |         Flags          |
 * +------------------------------------+------------------------+
 *
 * In a PDE, the physical address points to a page table.
 * In a PTE, the physical address points to a data or code page.
 * The important flags are listed below.
 * When a PDE or PTE is not "present", the other flags are
 * ignored.
 * A PDE or PTE that is initialized to 0 will be interpreted as
 * "not present", which is just fine.
 */
#define PTE_FLAGS 0x00000fff /* Flag bits. */
#define PTE_ADDR 0xfffff000 /* Address bits. */
#define PTE_AVL 0x00000e00 /* Bits available for OS use. */
#define PTE_P 0x1 /* 1=present, 0=not present. */
#define PTE_W 0x2 /* 1=read/write, 0=read-only. */
#define PTE_U 0x4 /* 1=user/kernel, 0=kernel only. */
#define PTE_A 0x20 /* 1=accessed, 0=not acccessed. */
#define PTE_D 0x40 /* 1=dirty, 0=not dirty (PTEs only). */

#ifdef VM

/* Information on non-present pages is stored in the page table entries within
 * the page table. The following formats are used:
 *
 * 31                     ZEROED OUT PAGE               5 4  3   0
 * +-----------------------------------------------------+--+----+
 * |                     AUXILARY DATA                   |ZW|1000|
 * +-----------------------------------------------------+--+----+
 * ZW bit (zeroed & writeable) determines the writeability of the page.
 * Auxiliary data is used when setting up user address space when executing a
 * new process.
 *
 * 31                  SWAPPED OUT STACK PAGE               3 2  0
 * +---------------------------------------------------------+---+
 * |                      SWAP SLOT ID                       |100|
 * +---------------------------------------------------------+---+
 * The swap slot id is used with swap_load(swap_id) to identify swapped
 * pages & load them back into memory.
 *
 * 31               MMAP/LAZY PAGE (NOT IN MEMORY)           2 1 0
 * +-+-----------------------------------------------------------+
 * |                     POINTER TO STRUCT                    |10|
 * +-+-----------------------------------------------------------+
 * Contains a pointer to a user_mmap or lazy_page struct. Due to pointer
 * alignment by malloc, the last two bits of any malloc will be zero. Hence we
 * can use these to identify (10 = pointer & frame not present).
 *
 * To identify which type of struct, a value at the top of the malloc identifies
 * the following struct.
 *
 *             +-----------+                +-----------+
 * Pointer ==> |    true   | OR Pointer ==> |   false   |
 *             +-----------+                +-----------+
 *             | lazy_page |                | user_mmap |
 *             |    ...    |                |    ...    |
 *             +-----------+                +-----------+
 *
 * This is easy to extend by using more bits (e.g enum instead of bool) for
 * implementation of different page types such as COW (Copy on write) shared
 * pages.
 */

#define PTE_PTR 0x2 /* 1=pointer pte (lazy/mmap page), 0=not. */
#define PTE_S 0x4 /* 1=in swap, 0=not in swap. */
#define PTE_Z 0x8 /* 1=should be zeroed, 0=shouldn't be zeroed. */
#define PTE_ZW 0x10 /* Zeroed page 1=writeable, 0=read-only. */
#define PTE_ZAUX_SHIFT 5 /* Bits to shift aux pte by in zeroed pte. */
#define PTE_SWAPID_SHIFT 3 /* Bits to shift swap pte by to get swap id. */
#define PTE_PTRMASK 0xfffffffc /* Mask to get pointer for lazy/mmap page. */

enum page_type { NOTSET, ZEROED, SWAPPED, MMAPED, LAZY, PAGEDIN };

#endif

/* Returns a PDE that points to page table PT. */
static inline uint32_t pde_create(uint32_t *pt)
{
	ASSERT(pg_ofs(pt) == 0);
	return vtop(pt) | PTE_U | PTE_P | PTE_W;
}

/* Returns a pointer to the page table that page directory entry PDE.
 * PDE must be present.
 */
static inline uint32_t *pde_get_pt(uint32_t pde)
{
	ASSERT(pde & PTE_P);
	return ptov(pde & PTE_ADDR);
}

/* Returns a PTE that points to the kernel page, PAGE.
 * If WRITABLE=1 the page is read/write, otherwise readonly.
 * The page will be usable only by ring 0 code (the kernel).
 */
static inline uint32_t pte_create_kernel(void *page, bool writable)
{
	ASSERT(pg_ofs(page) == 0);
	return vtop(page) | PTE_P | (writable ? PTE_W : 0);
}

/* Returns a PTE that points to the user page, PAGE.
 * If WRITABLE=1 the page is read/write, otherwise readonly.
 * The page will be usable by both user and kernel code.
 */
static inline uint32_t pte_create_user(void *page, bool writable)
{
	return pte_create_kernel(page, writable) | PTE_U;
}

/* Return a non-present page table entry (all zeros). */
static inline uint32_t pte_create_not_present(void)
{
	return 0;
}

/* Return true if the page table entry PTE is writeable. */
static inline bool pte_is_writable(uint32_t pte)
{
	return pte & PTE_W;
}

/* Return true if the page table entry PTE has its dirty bit set. */
static inline bool pte_is_dirty(uint32_t pte)
{
	return pte & PTE_D;
}

/* Sets the dirty bit of a page table entry PTE to DIRTY. */
static inline uint32_t pte_set_dirty(uint32_t pte, bool dirty)
{
	return dirty ? pte | PTE_D : pte & ~PTE_D;
}

#ifdef VM

/* Extracts the pointer to a lazy/mmap page from the page table entry PTE. */
static inline void *pte_get_pointer(uint32_t pte)
{
	return ptov(pte & PTE_PTRMASK);
}

/* Return the type of a given page table entry PTE. */
static inline enum page_type pte_get_type(uint32_t pte)
{
	if (pte & PTE_P)
		return PAGEDIN;
	if (pte & PTE_PTR)
		return *(bool *)pte_get_pointer(pte) ? LAZY : MMAPED;
	if (pte & PTE_S)
		return SWAPPED;
	if (pte & PTE_Z)
		return ZEROED;
	return NOTSET;
}

/* Create a pte entry for a swap of id SWAP_ID (swap id in bits [31-3]). */
static inline uint32_t pte_create_swap(swapid_t swap_id)
{
	return (swap_id << PTE_SWAPID_SHIFT) | PTE_S;
}

/* Get the swap id from a pte (PTE) representing a swapped out page. */
static inline uint32_t pte_get_swapid(uint32_t pte)
{
	ASSERT(pte_get_type(pte) == SWAPPED);
	return pte >> PTE_SWAPID_SHIFT;
}

/* Get pointer to user_mmap struct from page table entry PTE. */
static inline struct user_mmap *pte_get_user_mmap(uint32_t pte)
{
	ASSERT(pte_get_type(pte) == MMAPED);
	return pte_get_pointer(pte);
}

_Static_assert(PHYS_BASE >= (void *)0x80000000, "PHYS_BASE must be above 2GB");
/* Create a page table entry for an mmaped page that is not in memory using the
 * pointer to the user_mmap MMAP.
 *
 * Pointer bits [31-3] are in pte bits [31-3].
 */
static inline uint32_t pte_create_mmaped(struct user_mmap *mmap)
{
	ASSERT((vtop(mmap) & PTE_PTRMASK) == vtop(mmap));
	return vtop(mmap) | PTE_PTR;
}

/* Create a page table entry for a lazy loaded page. Will contain a pointer to
 * the lazy_load struct.
 *
 * Pointer bits [31-3] are in tpe bits [31-3].
 */
static inline uint32_t pte_create_lazy_load(struct lazy_load *lazy)
{
	ASSERT((vtop(lazy) & PTE_PTRMASK) == vtop(lazy));
	return vtop(lazy) | PTE_PTR;
}

/* Get lazy load pointer fro page table entry PTE. */
static inline struct lazy_load *pte_get_lazy_load(uint32_t pte)
{
	ASSERT(pte_get_type(pte) == LAZY);
	return pte_get_pointer(pte);
}

/* Create a page table entry for a zeroed out page, of writability WRITEABLE.
 * AUX is the additional data that can be put in the free space of the zeroed
 * page, for use by a certain thread. AUX cannot occupy more than 27 bits.
 */
static inline uint32_t pte_create_zeroed(bool writable, uint32_t aux)
{
	ASSERT(((aux << PTE_ZAUX_SHIFT) >> PTE_ZAUX_SHIFT) == aux);
	return (PTE_ZW * writable) | aux << PTE_ZAUX_SHIFT | PTE_Z;
}

/* Return the writability of the page associated with PTE. */
static inline bool pte_is_zeroed_writeable(uint32_t pte)
{
	ASSERT(pte_get_type(pte) == ZEROED);
	return (pte & PTE_ZW);
}

/* Returns the aux portion of the zeroed entry. */
static inline uint32_t pte_get_zeroed_aux(uint32_t pte)
{
	ASSERT(pte_get_type(pte) == ZEROED);
	return pte >> PTE_ZAUX_SHIFT;
}

#endif

/* Set a page table entry PTE's access to ACCESSED. */
static inline uint32_t pte_set_accessed(uint32_t pte, bool accessed)
{
	if (accessed)
		return pte |= PTE_A;
	else
		return pte &= ~(uint32_t)PTE_A;
}

/* Return true if the page table entry PTE has been accessed. */
static inline bool pte_is_accessed(uint32_t pte)
{
	return pte & PTE_A;
}

/* Returns true if the pte is accessible to user. */
static inline bool pte_is_user(uint32_t pte)
{
	return pte & PTE_U;
}

/* Returns a pointer to the page that page table entry PTE points to. */
static inline void *pte_get_page(uint32_t pte)
{
	if (pte & PTE_P)
		return ptov(pte & PTE_ADDR);
	return NULL;
}

#endif /* threads/pte.h */
