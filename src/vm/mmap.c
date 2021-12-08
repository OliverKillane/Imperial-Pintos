#include "vm/mmap.h"
#include <hash.h>
#include <stdint.h>
#include <string.h>
#include "filesys/inode.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"

/* The mmap system maps files to sections of virtual memory in processes'
 * address space.
 *
 * To reduce memory usage mmaps are shared (multiple processes' mmaping the
 * same file share frames). This is done on a per-page basis.
 *
 * - Each page to be mmaped from a file is hashed (inode, offset & length).
 * - The MMAPS hashmap is used to check if the file is already being used by
 *   other processes through a SHARED_MMAP (if not register a new one).
 * - SHARED_MMAPs contain file information for the mapped page, as well as a
 *   list of USER_MMAPs. Each USER_MMAP manages a different page table entry
 *   that is using the SHARED_MMAP.
 *
 *               +--[map]--+                          +-->+---[list]---+
 * +--[key]--+   |         |                          |   | user_mmap  +--> PTE
 * |writable |   |   ...   |   +---[shared-mmap]---+  |   +------------+
 * | length  +-->|  mmaps  +-->|  mmap_system_elem |  |   | user_mmap  +--> PTE
 * | offset  |   |   ...   |   |        ...        |  |   +------------+
 * |  inode  |   |         |   | user_mmaped_pages +--+   | user_mmap  +--> PTE
 * +---------+   |         |   +-------------------+      +------------+
 *               +---------+                              | user_mmap  +--> PTE
 *                                                        +------------+
 */

/* hashmap of all SHARED_MMAPs. */
struct hash mmaps;

/* Lock to synchronize access to the MMAPS hash map. */
struct lock mmaps_lock;

/* Handles the sharing of an mmaped page. Contains a list of user_mmaps for each
 * page using the shared_mmap. Access synchronized through lock.
 */
struct shared_mmap {
	/* Members used for hashing. */
	struct hash_elem mmap_system_elem; /* elem for MMAPS hashmap. */
	struct file *file; /* File to read/write from. */
	off_t file_offset; /* Offset within file mmapped. */
	int16_t length; /* Number of bytes used in the page. */
	bool writable; /* Mmap writeability. */

	/* Mmap page information. */
	bool dirty; /* preserves dirty bit of users unmapping. */
	struct lock lock; /* general lock for this SHARED_MMAP. */
	struct list user_mmaped_pages; /* list of users of that mmap. */
};

/* Handles the user's page table entry for an mmaped page. */
struct user_mmap {
	bool is_lazy; /* Determines struct type (see 'pte.h' lazy/mmap page).*/
	struct list_elem mmap_id_elem; /* Elem of the bookkeeping list. */
	struct list_elem shared_mmap_elem; /* Elem of the list of mmap users. */
	struct shared_mmap *shared_mmap; /* Pointer to the mmap for that user. */
	uint32_t *pd; /* Page directory of the mmapped page. */
	void *vpage; /* User page within referenced. */
};

static void write_back(struct shared_mmap *shared_mmap, void *kpage);
static bool mmap_or_ptes(struct shared_mmap *shared_mmap,
												 bool (*condition)(uint32_t *, const void *));

/* Ensures that the pointer to the USER_MMAP will fit in a pte */
_Static_assert(_Alignof(struct user_mmap) >= 4,
							 "user_mmap needs to be 4 bytes aligned");

/* Access functions for the MMAPS hash. */
hash_hash_func shared_mmap_hash_func;
hash_less_func shared_mmap_less_func;

/* Initialise the mmapping system. */
void mmap_init(void)
{
	hash_init(&mmaps, shared_mmap_hash_func, shared_mmap_less_func, NULL);
	lock_init(&mmaps_lock);
}

/* Registers a new mmaped page for FILE at OFFSET.
 *
 * If a SHARED_MMAP already exists with the same file, offset, length &
 * readability, a USER_MMAP is created and attached to it for sharing.
 *
 * If none exists, a new SHARED_MMAP is created with the USER_MMAP attached.
 *
 * FILE     - The file to be mmaped from.
 * OFFSET   - The offset (for FILE_SEEK()) within the file.
 * LENGTH   - Number of bytes to read to the page (at most PGSIZE), and
 *            remaining bytes are zeroed.
 * WRITABLE - The writability, for user mmaps it is always true. We also use
 *            mmaps for readonly parts of executable files, where this is set to
 *            false.
 * PD VPAGE - The page directory and virtual address where the page should be
 *            installed. The corresponding page table entry is managed by the
 *            USER_MMAP.
 * MMAPING_ - The bookkeeping list used by processes to keep a record of which
 * LIST       mmaps they have open. Used when unmapping.
 *
 */
bool mmap_register(struct file *file, off_t offset, int16_t length,
									 bool writable, uint32_t *pd, void *vpage,
									 struct list *mmaping_list)
{
	ASSERT(length <= PGSIZE);

	/* Preparing the USER_MMAP. If the malloc() fails here, then we cannot
	 * proceed further and fail the registering.
	 */
	struct user_mmap *user_mmap = malloc(sizeof(struct user_mmap));
	if (!user_mmap)
		return false;

	/* USER_MMAP setup. */
	user_mmap->is_lazy = false;
	user_mmap->pd = pd;
	user_mmap->vpage = vpage;

	/* Create key for MMAPS hashmap access. */
	struct shared_mmap key = {
		.file = file, .file_offset = offset, .length = length, .writable = writable
	};

	/* Acquire the MMAPS_LOCK for exclusive access to the MMAPS hashmap. */
	lock_acquire(&mmaps_lock);

	/* If a SHARED_MMAP already exists, register the USER_MMAP with it, if not
	 * create a new SHARED_MMAP to register with.
	 */
	struct shared_mmap *shared_mmap =
					hash_entry(hash_find(&mmaps, &key.mmap_system_elem),
										 struct shared_mmap, mmap_system_elem);
	if (!shared_mmap) {
		shared_mmap = malloc(sizeof(struct shared_mmap));

		if (!shared_mmap) {
			lock_release(&mmaps_lock);
			free(user_mmap);
			return false;
		}

		/* Check in case the allocation of a new page table fails */
		if (!pagedir_set_mmaped_page(pd, vpage, user_mmap)) {
			lock_release(&mmaps_lock);
			free(user_mmap);
			free(shared_mmap);
			return false;
		}

		/* If not writeable, deny writes to file. This prevents data of read-only
		 * mmaps changing unexpectedly when the file backing the mmap is written to.
		 */
		filesys_enter();
		shared_mmap->file = file_reopen(file);
		if (!shared_mmap->file) {
			filesys_exit();
			lock_release(&mmaps_lock);
			free(user_mmap);
			free(shared_mmap);
			pagedir_clear_page(pd, vpage);
			return false;
		}
		if (!writable)
			file_deny_write(shared_mmap->file);
		filesys_exit();

		shared_mmap->file_offset = offset;
		shared_mmap->length = length;
		shared_mmap->writable = writable;
		shared_mmap->dirty = false;
		lock_init(&shared_mmap->lock);

		/* Insert the USER_MMAP into the SHARED_MMAP */
		list_init(&shared_mmap->user_mmaped_pages);
		list_push_back(&shared_mmap->user_mmaped_pages,
									 &user_mmap->shared_mmap_elem);

		/* Insert into the MMAPS hashmap & release lock to allow access. */
		if (hash_insert(&mmaps, &shared_mmap->mmap_system_elem))
			NOT_REACHED();
		lock_release(&mmaps_lock);
	} else {
		/* Release access to MMAPS hashmap, but keep continuous exclusive access to
		 * SHARED_MMAP to prevent removal.
		 */
		lock_acquire(&shared_mmap->lock);
		lock_release(&mmaps_lock);

		/* Invariant: SHARED_MMAPs must have at least one USER_MMAP. */
		ASSERT(!list_empty(&shared_mmap->user_mmaped_pages));

		/* This is an initialization of the new page table entry (pte) for the
		 * USER_MMAP:
		 *   - Access to the list of users is synchronized by acquiring the
		 *     SHARED_MMAP lock.
		 *   - Access to the page table entry of the other user is synchronized
		 *     as in order to modify a PTE associated with an MMAP, the SHARED_MMAP
		 *     lock must be acquired.
		 * We can then use the page table entry of another sharing the mmap as:
		 *   - If the page table entry is not present, we can update the page table
		 *     entry with a pointer to the new USER_MMAP (See pte.h).
		 *   - If the page table entry is present, we can copy it to the current
		 *     process as the physical address, writability, etc, will be
		 *     the same. The dirty bit can be copied as the SHARED_MMAP's dirtiness
		 *     is the disjunction (OR) of all pte's dirty bits.
		 */
		struct user_mmap *other_user_mmap =
						list_entry(list_front(&shared_mmap->user_mmaped_pages),
											 struct user_mmap, shared_mmap_elem);
		void *kpage = pagedir_get_page(other_user_mmap->pd, other_user_mmap->vpage);

		bool pagedir_success;
		if (kpage)
			pagedir_success = pagedir_set_page(
							pd, vpage, kpage,
							pagedir_is_writable(other_user_mmap->pd, other_user_mmap->vpage));
		else
			pagedir_success = pagedir_set_mmaped_page(pd, vpage, user_mmap);

		/* If pagedir cannot be set, fail allocation. */
		if (!pagedir_success) {
			lock_release(&shared_mmap->lock);
			free(user_mmap);
			return false;
		}

		/* Finalise the setup of the USER_MMAP by inserting it into the list of
		 * USER_MMAPED_PAGES in the SHARED_MMAP.
		 */
		list_push_back(&shared_mmap->user_mmaped_pages,
									 &user_mmap->shared_mmap_elem);

		lock_release(&shared_mmap->lock);
	}

	/* USER_MMAP's pointer to the SHARED_MMAP is only used by the thread that
	 * owns the USER_MMAP, so no synchronisation needed for setting it.
	 */
	user_mmap->shared_mmap = shared_mmap;
	list_push_back(mmaping_list, &user_mmap->mmap_id_elem);
	return true;
}

/* Unregisters an mmap. Sets the page table entry to unused, effectively freeing
 * its ownership, removes itself from the bookkeeping list and frees the
 * USER_MMAP struct.
 *
 * If the unregistering process is the final one using the SHARED_MMAP, then it
 * is removed from the mmaping system.
 */
void mmap_unregister(struct user_mmap *user_mmap)
{
	struct shared_mmap *shared_mmap = user_mmap->shared_mmap;

	/* We acquire this lock to ensure that no one can register themselves into
	 * the SHARED_MMAP while we are evicting it, and no other processes can search
	 * the MMAPs hashmap for the SHARED_MMAP (as we may remove it).
	 */
	lock_acquire(&mmaps_lock);
	lock_acquire(&shared_mmap->lock);

	/* If the USER_MMAP is the last, remove the SHARED_MMAP, else just remove the
	 * USER_MMAP.
	 */
	if (list_elem_alone(&user_mmap->shared_mmap_elem)) {
		lock_release(&shared_mmap->lock);

		/* Remove the SHARED_MMAP, release the MMAPS hashmap afterwards as the
		 * SHARED_MMAP is no longer in the mmaping system, so only this process can
		 * access.
		 */
		hash_delete(&mmaps, &shared_mmap->mmap_system_elem);
		lock_release(&mmaps_lock);

		/* Get a FRAME_LOCK on the frame used by the SHARED_MMAP.
		 * If frame lock succeeds:
		 *    Write back to file and free the frame. The free SHARED_MMAP.
		 * If frame lock fails:
		 *    Frame has been evicted (already written back to file), only need to
		 *    free SHARED_MMAP.
		 */
		void *kpage = pagedir_get_page(user_mmap->pd, user_mmap->vpage);
		if (kpage && frame_lock_mmaped(shared_mmap, kpage)) {
			filesys_enter();
			write_back(shared_mmap, kpage);
			file_close(shared_mmap->file);
			filesys_exit();

			palloc_free_page(kpage);
		}
		free(shared_mmap);
	} else {
		/* We are not destroying the SHARED_MMAP, so it is fine for other processes
		 * to attempt to use it. Hence we release the lock on the MMAPS hashmap.
		 */
		lock_release(&mmaps_lock);

		/* Remove the USER_MMAP from the SHARED_MMAP. If the page is dirty,
		 * preserve this in the DIRTY feild of the SHARED MMAP.
		 */
		list_remove(&user_mmap->shared_mmap_elem);
		if (pagedir_get_page_type(user_mmap->pd, user_mmap->vpage) == PAGEDIN)
			shared_mmap->dirty |= pagedir_is_dirty(user_mmap->pd, user_mmap->vpage);

		lock_release(&shared_mmap->lock);
	}

	/* USER_MMAP is no longer coupled to any SHARED_MMAP at this point, so we do
   * not need to synchronize it.
	 */
	pagedir_clear_page(user_mmap->pd, user_mmap->vpage);
	list_remove(&user_mmap->mmap_id_elem);
	free(user_mmap);
}

/* Function for converting the elem of the list of mmapings provided in
 * MMAPING_LIST in mmap_register() to an entry.
 */
struct user_mmap *mmap_list_entry(struct list_elem *elem)
{
	return list_entry(elem, struct user_mmap, mmap_id_elem);
}

/* Loads the mmap into the given page and updates all its users.
 * KPAGE must be page-locked.
 */
void mmap_load(struct user_mmap *user_mmap)
{
	void *kpage = frame_get();
	struct shared_mmap *shared_mmap = user_mmap->shared_mmap;

	lock_acquire(&shared_mmap->lock);

	/* If the mmap is already loaded, can just return. */
	if (pagedir_get_page_type(user_mmap->pd, user_mmap->vpage) != MMAPED) {
		lock_release(&shared_mmap->lock);
		return;
	}

	/* KPAGE is frame locked (cannot be evicted) and the SHARED_MMAP lock is
	 * acquired, so exclusive access to ensured. Hence it is safe to read.
	 */
	filesys_enter();
	file_seek(shared_mmap->file, shared_mmap->file_offset);
	file_read(shared_mmap->file, kpage, shared_mmap->length);
	filesys_exit();

	/* If the section of file is not a whole page, fill the remainder of the page
	 * with zeros.
	 */
	memset(kpage + shared_mmap->length, 0, PGSIZE - shared_mmap->length);

	/* Update every page table entry connected to that SHARED_MMAP. Exclusive
	 * access to pte is ensured since we have the lock on the SHARED_MMAP as well.
	 */
	for (struct list_elem *elem = list_begin(&shared_mmap->user_mmaped_pages);
			 elem != list_end(&shared_mmap->user_mmaped_pages);
			 elem = list_next(elem)) {
		struct user_mmap *user_mmap =
						list_entry(elem, struct user_mmap, shared_mmap_elem);
		pagedir_set_page(user_mmap->pd, user_mmap->vpage, kpage,
										 shared_mmap->writable);
	}

	/* Release exclusive access, frame unlock so the page can be evicted. */
	lock_release(&shared_mmap->lock);
	frame_unlock_mmaped(shared_mmap, kpage);
}

/* Evict a frame for an mmaped file, informing all page table entries using it
 * KPAGE must be frame locked before calling. USED_QUEUE_LOCK is used to release
 * access to the used_queue, so that other evictions can occur.
 * USED_QUEUE_LOCK must be released before the funtion returns.
 */
void mmap_frame_evict(void *kpage, struct shared_mmap *shared_mmap,
											struct lock *used_queue_lock)
{
	lock_acquire(&shared_mmap->lock);

	/* Update every page table entry connected to that SHARED_MMAP. Exclusive
	 * access to each entry is enforced by the SHARED_MMAP lock.
	 */
	for (struct list_elem *elem = list_begin(&shared_mmap->user_mmaped_pages);
			 elem != list_end(&shared_mmap->user_mmaped_pages);
			 elem = list_next(elem)) {
		struct user_mmap *user_mmap =
						list_entry(elem, struct user_mmap, shared_mmap_elem);
		pagedir_set_mmaped_page(user_mmap->pd, user_mmap->vpage, user_mmap);
	}

	/* For letting the used queue take possible swappable pages to evict */
	lock_release(used_queue_lock);

	/* Write back, check if lock held (if the current process has page faulted
	 * while writing to file system, do not re-acquire the lock).
	 */
	filesys_enter();
	write_back(shared_mmap, kpage);
	filesys_exit();

	lock_release(&shared_mmap->lock);
}

/* Checks if the shared page SAHRED_MMAP was accessed by any of its processes.
 * Called in FRAME_GET().
 */
bool mmap_frame_was_accessed(struct shared_mmap *shared_mmap)
{
	lock_acquire(&shared_mmap->lock);

	/* Check if any page table entries using the mmap frame have been accessed. */
	bool accessed = mmap_or_ptes(shared_mmap, pagedir_is_accessed);
	lock_release(&shared_mmap->lock);
	return accessed;
}

/* Resets the access bits of all user pages sharing the mmaping.
 * Called in FRAME_GET().
 */
void mmap_frame_reset_accessed(struct shared_mmap *shared_mmap)
{
	lock_acquire(&shared_mmap->lock);

	/* Update every page table entry connected to that SHARED_MMAP, setting
	 * accessed as false. Exclusive access to page table entries is enforced
	 * through the SHARED_MMAP lock.
	 */
	for (struct list_elem *elem = list_begin(&shared_mmap->user_mmaped_pages);
			 elem != list_end(&shared_mmap->user_mmaped_pages);
			 elem = list_next(elem)) {
		struct user_mmap *user_mmap =
						list_entry(elem, struct user_mmap, shared_mmap_elem);
		pagedir_set_accessed(user_mmap->pd, user_mmap->vpage, false);
	}
	lock_release(&shared_mmap->lock);
}

/* Hashing function for shared_mmap struct. */
unsigned int shared_mmap_hash_func(const struct hash_elem *shared_mmap_raw,
																	 void *aux UNUSED)
{
	const struct shared_mmap *shared_mmap =
					hash_entry(shared_mmap_raw, struct shared_mmap, mmap_system_elem);
	struct {
		bool writable;
		int16_t length;
		off_t file_offset;
		struct inode *inode;
	} hash_helper;
	memset(&hash_helper, 0, sizeof hash_helper);
	hash_helper.writable = shared_mmap->writable;
	hash_helper.length = shared_mmap->length;
	hash_helper.file_offset = shared_mmap->file_offset;
	hash_helper.inode = file_get_inode(shared_mmap->file);
	return hash_bytes(&hash_helper, sizeof hash_helper);
}

/* Comparison function for shared_mmap struct. */
bool shared_mmap_less_func(const struct hash_elem *a_raw,
													 const struct hash_elem *b_raw, void *aux UNUSED)
{
	const struct shared_mmap *a =
					hash_entry(a_raw, struct shared_mmap, mmap_system_elem);
	const struct shared_mmap *b =
					hash_entry(b_raw, struct shared_mmap, mmap_system_elem);
	if (a->length != b->length)
		return a->length < b->length;
	if (a->file_offset != b->file_offset)
		return a->file_offset < b->file_offset;
	if (a->writable != b->writable)
		return a->writable < b->writable;
	return file_get_inode(a->file) < file_get_inode(b->file);
}

/* Updates the file that we are mmaping if the file is writable and dirty
 * We do not alter dirty page table entries as the two cases of using this
 * function are:
 * (A) last process unregistering:
 *       page table entry will not be used for this mmap again.
 * (B) Page replacement:
 *       Dirty bits overwritten by user_mmap pointer.
 */
static void write_back(struct shared_mmap *shared_mmap, void *kpage)
{
	if (!shared_mmap->writable)
		return;
	if (!shared_mmap->dirty && !mmap_or_ptes(shared_mmap, pagedir_is_dirty))
		return;

	file_seek(shared_mmap->file, shared_mmap->file_offset);
	file_write(shared_mmap->file, kpage, shared_mmap->length);

	shared_mmap->dirty = false;
}

/* Does an or over all ptes in that SHARED_MMAP using the given function. */
static bool mmap_or_ptes(struct shared_mmap *shared_mmap,
												 bool (*condition)(uint32_t *, const void *))
{
	for (struct list_elem *elem = list_begin(&shared_mmap->user_mmaped_pages);
			 elem != list_end(&shared_mmap->user_mmaped_pages);
			 elem = list_next(elem)) {
		struct user_mmap *user_mmap =
						list_entry(elem, struct user_mmap, shared_mmap_elem);
		if (condition(user_mmap->pd, user_mmap->vpage))
			return true;
	}
	return false;
}
