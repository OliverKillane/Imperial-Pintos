#include "userprog/pagedir.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "threads/init.h"
#include "threads/pte.h"
#include "threads/palloc.h"
#include "threads/synch.h"

/* Functions for managing the page directory.
 *
 * Where possible functions set the page table entries atomically/in one store.
 * this eliminates the possibility of mangled entries (parts of two separate
 * entry set operations).
 */

static uint32_t *active_pd(void);
static void invalidate_pagedir(uint32_t *);

/* Creates a new page directory that has mappings for kernel virtual addresses,
 * but none for user virtual addresses. Returns the new page directory, or a
 * null pointer if memory allocation fails.
 */
uint32_t *pagedir_create(void)
{
	uint32_t *pd = palloc_get_page(0);
	if (pd)
		memcpy(pd, init_page_dir, PGSIZE);
	return pd;
}

/* Destroys page directory PD, freeing all the pages, swap entries and
 * lazy-loadings it references.
 *
 * If using VM, all mmapings must have been unmapped prior to calling.
 */
void pagedir_destroy(uint32_t *pd)
{
	uint32_t *pde;
	if (!pd)
		return;

	ASSERT(pd != init_page_dir);
	for (pde = pd; pde < pd + pd_no(PHYS_BASE); pde++)
		if (*pde & PTE_P) {
			uint32_t *pt = pde_get_pt(*pde);
			uint32_t *pte;

			for (pte = pt; pte < pt + PGSIZE / sizeof *pte; pte++) {
#ifdef VM
				uint32_t pte_val = *pte;
				barrier();
				switch (pte_get_type(pte_val)) {
				/* For a page in page (currently in memory):
				 * 1. Get the frame associated with the page.
				 * 2. Attempt to frame lock the page:
				 *	a. If successful -> exclusive access, page replacement cannot
				 *                       evict/alter the frame
				 *	b. Else -> page has been evicted to swap, only the current
				 *						 process can load it back, so mutually exclusive access.
				 * 3. Use palloc to free the frame if in memory, else fall-through to
				 *    freeing a swapped page.
				 */
				case PAGEDIN: {
					void *kpage = pte_get_page(pte_val);
					void *vpage = (void *)(uintptr_t)((pde - pd) * PGSIZE / sizeof *pte *
																						PGSIZE) +
												(pte - pt) * PGSIZE;
					if (frame_lock_swappable(pd, vpage, kpage)) {
						frame_free(kpage);
						break;
					}
				}
				/* fall-through */

				/* For a swapped out page mark the swap slot as free.*/
				case SWAPPED:
					swap_free(pte_get_swapid(*pte));
					break;
				/* For a lazy page, close the file and free its associated struct
				 * (inside LAZY_FREE).
				 */
				case LAZY:
					lazy_free(pte_get_lazy_load(*pte));
					break;
				/* For any other page, no action is required (zeroed out page table
				 * entry).
				 */
				default:
					ASSERT(pte_get_type(pte_val) != MMAPED);
					break;
				}
#else
				palloc_free_page(pte_get_page(*pte));
#endif
			}
			palloc_free_page(pt);
		}
	palloc_free_page(pd);
}

/* Returns the address of the page table entry for virtual
 * address VADDR in page directory PD.
 * If PD does not have a page table for VADDR, behavior depends
 * on CREATE.  If CREATE is true, then a new page table is
 * created and a pointer into it is returned.  Otherwise, a null
 * pointer is returned.
 */
static uint32_t *lookup_page(uint32_t *pd, const void *vaddr, bool create)
{
	uint32_t *pt, *pde;

	ASSERT(pd);

	/* Shouldn't create new kernel virtual mappings. */
	ASSERT(!create || is_user_vaddr(vaddr));

	/* Check for a page table for VADDR. If missing, create one if requested. */
	pde = pd + pd_no(vaddr);
	if (*pde == 0) {
		if (create) {
			pt = palloc_get_page(PAL_ZERO);
			if (!pt)
				return NULL;

			*pde = pde_create(pt);
		} else {
			return NULL;
		}
	}

	/* Return the page table entry. */
	pt = pde_get_pt(*pde);
	return &pt[pt_no(vaddr)];
}

/* Adds a mapping in page directory PD from user virtual page UPAGE to the
 * physical frame identified by kernel virtual address KPAGE. KPAGE should
 * probably be a page obtained from the user pool with palloc_get_page().
 * If WRITABLE is true, the new page is read/write; otherwise it is read-only.
 * Returns true if successful, false if memory allocation failed.
 * Atomically sets the PTE to the correct value.
 */
bool pagedir_set_page(uint32_t *pd, void *upage, void *kpage, bool writable)
{
	uint32_t *pte;

	ASSERT(pg_ofs(upage) == 0);
	ASSERT(pg_ofs(kpage) == 0);
	ASSERT(is_user_vaddr(upage));
	ASSERT(vtop(kpage) >> PTSHIFT < init_ram_pages);
	ASSERT(pd != init_page_dir);

	pte = lookup_page(pd, upage, true);
	if (!pte)
		return false;
	uint32_t pte_val = pte_create_user(kpage, writable);
	barrier();
	*pte = pte_val;
	invalidate_pagedir(pd);
	return true;
}

/* Looks up the physical address that corresponds to user virtual address UADDR
 * in PD.  Returns the kernel virtual address corresponding to that physical
 * address, or a null pointer if UADDR is unmapped. Atomically gets the data
 * from the PTE.
 */
void *pagedir_get_page(uint32_t *pd, const void *uaddr)
{
	uint32_t *pte;

	ASSERT(is_user_vaddr(uaddr));

	pte = lookup_page(pd, uaddr, false);
	if (pte) {
		void *kpage = pte_get_page(*pte);
		barrier();
		if (kpage)
			return kpage + pg_ofs(uaddr);
	}
	return NULL;
}

/* Marks user virtual page UPAGE "not present" in page directory PD.  Later
 * accesses to the page will fault. Causes the page fault to fail on it. UPAGE
 * need not be mapped. Atomically sets the PTE to the correct value.
 */
void pagedir_clear_page(uint32_t *pd, void *upage)
{
	uint32_t *pte;

	ASSERT(pg_ofs(upage) == 0);
	ASSERT(is_user_vaddr(upage));

	pte = lookup_page(pd, upage, false);
	if (pte) {
		*pte = pte_create_not_present();
		invalidate_pagedir(pd);
	}
}

#ifdef VM

/* Sets the PTE for virtual page VPAGE in PD to lazy-zeroed, with writability
 * set to WRITABLE. Atomically sets the PTE to the correct value.
 */
bool pagedir_set_zeroed_page(uint32_t *pd, void *vpage, bool writable,
														 uint32_t aux)
{
	uint32_t *pte;

	ASSERT(pg_ofs(vpage) == 0);
	ASSERT(is_user_vaddr(vpage));

	pte = lookup_page(pd, vpage, true);
	if (!pte)
		return false;
	uint32_t pte_val = pte_create_zeroed(writable, aux);
	barrier();
	*pte = pte_val;
	invalidate_pagedir(pd);
	return true;
}

/* Atomically gets and returns if the page is lazy-zeroed and should be loaded
 * as writable.
 */
bool pagedir_is_zeroed_writable(uint32_t *pd, const void *vpage)
{
	uint32_t *pte;

	ASSERT(pg_ofs(vpage) == 0);
	ASSERT(is_user_vaddr(vpage));

	pte = lookup_page(pd, vpage, true);
	if (!pte)
		return false;

	uint32_t pte_val = *pte;
	barrier();
	return pte_is_zeroed_writeable(pte_val);
}

/* Atomically get the aux field of
 * the page table entry of lazy-zeroed VPAGE in PD.
 */
uint32_t pagedir_get_zeroed_aux(uint32_t *pd, const void *vpage)
{
	uint32_t *pte;

	ASSERT(pg_ofs(vpage) == 0);
	ASSERT(is_user_vaddr(vpage));

	pte = lookup_page(pd, vpage, true);
	if (!pte)
		return false;

	uint32_t pte_val = *pte;
	barrier();
	return pte_get_zeroed_aux(pte_val);
}

/* Sets the PTE for virtual page VPAGE in PD to sitting in swap in the
 * slot SWAPID. Atomically sets the PTE to the correct value.
 */
bool pagedir_set_swapped_page(uint32_t *pd, void *vpage, swapid_t swapid)
{
	uint32_t *pte;

	ASSERT(pg_ofs(vpage) == 0);
	ASSERT(is_user_vaddr(vpage));

	pte = lookup_page(pd, vpage, true);
	if (!pte)
		return false;
	uint32_t pte_val = pte_create_swap(swapid);
	barrier();
	*pte = pte_val;
	invalidate_pagedir(pd);
	return true;
}

/* Sets the PTE for virtual page VPAGE in PD to mmaped with the handel to it
 * being USER_MMAP. Atomically sets the PTE to the correct value.
 */
bool pagedir_set_mmaped_page(uint32_t *pd, void *vpage,
														 struct user_mmap *mmaped_page)
{
	uint32_t *pte;

	ASSERT(pg_ofs(vpage) == 0);
	ASSERT(is_user_vaddr(vpage));

	pte = lookup_page(pd, vpage, true);
	if (!pte)
		return false;
	uint32_t pte_val = pte_create_mmaped(mmaped_page);
	barrier();
	*pte = pte_val;
	invalidate_pagedir(pd);
	return true;
}

/* Sets the PTE for virtual page VPAGE in PD to lazily loaded with the handle to
 * it being LAZY_PAGE. Atomically sets the PTE to the correct value.
 */
bool pagedir_set_lazy_page(uint32_t *pd, void *vpage,
													 struct lazy_load *lazy_page)
{
	uint32_t *pte;

	ASSERT(pg_ofs(vpage) == 0);
	ASSERT(is_user_vaddr(vpage));

	pte = lookup_page(pd, vpage, true);
	if (!pte)
		return false;
	uint32_t pte_val = pte_create_lazy_load(lazy_page);
	barrier();
	*pte = pte_val;
	invalidate_pagedir(pd);
	return true;
}

/* Returns the type of the PTE for virtual page VPAGE in PD. Gets the type of
 * PTE atomically.
 */
enum page_type pagedir_get_page_type(uint32_t *pd, const void *vpage)
{
	uint32_t *pte;

	ASSERT(pg_ofs(vpage) == 0);
	ASSERT(is_user_vaddr(vpage));

	pte = lookup_page(pd, vpage, false);
	if (!pte)
		return NOTSET;
	uint32_t pte_val = *pte;
	barrier();
	return pte_get_type(pte_val);
}

/* Returns a raw pte, so other PTE functions can be used to determine value.
 * This is only used for the page fault exception handler.
 */
uint32_t pagedir_get_raw_pte(uint32_t *pd, const void *vpage)
{
	uint32_t *pte = lookup_page(pd, vpage, false);
	if (!pte)
		return pte_create_not_present();
	return *pte;
}

#endif

/* Returns if the PTE for virtual page VPAGE in PD is dirty, that is, if the
 * page has been modified since the PTE was installed. Has to be called on a PTE
 * that is present. Gets the dirty bit from the PTE atomically.
 */
bool pagedir_is_dirty(uint32_t *pd, const void *vpage)
{
	uint32_t *pte = lookup_page(pd, vpage, false);
	ASSERT(pte);

	uint32_t pte_val = *pte;
	barrier();
	ASSERT(pte_get_page(pte_val));
	return pte_is_dirty(pte_val);
}

/* Set the dirty bit to DIRTY in the PTE for virtual page VPAGE in PD. Has to be
 * called on a PTE that is present. Atomically sets the PTE to the correct
 * value.
 */
void pagedir_set_dirty(uint32_t *pd, const void *vpage, bool dirty)
{
	uint32_t *pte = lookup_page(pd, vpage, false);
	ASSERT(pte);

	uint32_t pte_val = *pte;
	barrier();
	ASSERT(pte_get_page(pte_val));
	*pte = pte_set_dirty(pte_val, dirty);
	if (!dirty)
		invalidate_pagedir(pd);
}

/* Returns if the PTE for virtual page VPAGE in PD has been accessed recently
 * (between the time the PTE was installed and the last time it was cleared).
 * PTE must be present. Atomically checks the accessed bit of the PTE.
 */
bool pagedir_is_accessed(uint32_t *pd, const void *vpage)
{
	uint32_t *pte = lookup_page(pd, vpage, false);
	ASSERT(pte);

	uint32_t pte_val = *pte;
	barrier();
	ASSERT(pte_get_page(pte_val));
	return pte_is_accessed(pte_val);
}

/* Sets the accessed bit to ACCESSED in the PTE for virtual page VPAGE in PD.
 * Has to be called on a PTE that is present. Atomically sets the PTE to the
 * correct value.
 */
void pagedir_set_accessed(uint32_t *pd, const void *vpage, bool accessed)
{
	uint32_t *pte = lookup_page(pd, vpage, false);
	ASSERT(pte);

	uint32_t pte_val = *pte;
	barrier();
	ASSERT(pte_get_page(pte_val));
	*pte = pte_set_accessed(pte_val, accessed);
	if (!accessed)
		invalidate_pagedir(pd);
}

/* Returns if it is possile to write to the PTE for virtual page VPAGE in PD.
 * Has to be called on a PTE that is present. Atomically checks the writable bit
 * of the PTE.
 */
bool pagedir_is_writable(uint32_t *pd, const void *vpage)
{
	uint32_t *pte = lookup_page(pd, vpage, false);
	ASSERT(pte);

	uint32_t pte_val = *pte;
	barrier();
	ASSERT(pte_get_page(pte_val));
	return pte_is_writable(pte_val);
}

/* Loads page directory PD into the CPU's page directory base register. */
void pagedir_activate(uint32_t *pd)
{
	if (!pd)
		pd = init_page_dir;

	/* Store the physical address of the page directory into CR3
	 * aka PDBR (page directory base register).  This activates our
	 * new page tables immediately.  See [IA32-v2a] "MOV--Move
	 * to/from Control Registers" and [IA32-v3a] 3.7.5 "Base
	 * Address of the Page Directory".
	 */
	asm volatile("movl %0, %%cr3" : : "r"(vtop(pd)) : "memory");
}

/* Returns the currently active page directory. */
static uint32_t *active_pd(void)
{
	/* Copy CR3, the page directory base register (PDBR), into
	 * `pd'.
	 * See [IA32-v2a] "MOV--Move to/from Control Registers" and
	 * [IA32-v3a] 3.7.5 "Base Address of the Page Directory".
	 */
	uintptr_t pd;
	asm volatile("movl %%cr3, %0" : "=r"(pd));
	return ptov(pd);
}

/* Some page table changes can cause the CPU's translation
 * lookaside buffer (TLB) to become out-of-sync with the page
 * table.  When this happens, we have to "invalidate" the TLB by
 * re-activating it.
 *
 * This function invalidates the TLB if PD is the active page
 * directory.  (If PD is not active then its entries are not in
 * the TLB, so there is no need to invalidate anything.)
 */
static void invalidate_pagedir(uint32_t *pd)
{
	if (active_pd() == pd) {
		/* Re-activating PD clears the TLB.  See [IA32-v3a] 3.12
		 * "Translation Lookaside Buffers (TLBs)".
		 */
		pagedir_activate(pd);
	}
}
