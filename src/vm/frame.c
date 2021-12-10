#define PAL_USER_ENABLE

#include <list.h>
#include "threads/malloc.h"
#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/synch.h"

/* Frame Table Entry*/
struct fte {
	struct list_elem used_elem;
	bool is_swappable; /* True=evict to swap False=evict to filesys(mmap). */
	union { /* Pointer to owner of the frame. */
		struct shared_mmap *shared_mmap; /* Mmaped page owns the frame. */
		struct { /* Pagedir & virtual page own the frame. (Swappable) */
			uint32_t *pd;
			void *vpage;
		};
	};
};

/*                      FRAME TABLE:
 *   0   1   2   3   4   5   6   7   8   9  10  11  12    ...
 * +---+---+---+---+---+---+---+---+---+---+---+---+---+--
 * | P | P | L | F | L | P | P | F | F | F | F | F | F |  ...
 * +-+-+---+---+---+---+---+---+---+-+-+---+---+---+---+--
 *   |                               |
 *   +--> base of user pool          +--> (kpage - base) = index in frame table.
 *
 * Frames can be: Present (being used & unlocked - in USED_QUEUE).
 *                Locked (being used & eviction prevented - not in USED_QUEUE).
 *                Free (not present, can be taken by palloc_get_page(PAL_USER)).
 *
 * Frame table is allocated once. By using a large array to store entries the
 * lookup time is very low at the expense of memory.
 */
static struct fte *ftes;

/* Base of the user pool, used to map kpages to entries in FTES table. */
static uint8_t *user_base;

/* Lock to ensure mutually-exclusive access to the used_queue. */
struct lock used_queue_lock;

/* Passing a down with this semaphore ensures that there is at least one
 * frame that is not locked - (free in the palloc system or unlocked in the
 * USED_QUEUE).
 */
struct semaphore unlocked_frames;

/* Queue of all currently non-page locked frames. */
struct list used_queue;

static inline struct fte *kpage_to_fte(void *kpage);
static inline void *fte_to_kpage(struct fte *fte);

static inline bool frame_was_accessed(struct fte *entry);
static inline void frame_reset_accessed(struct fte *entry);
static void frame_reset(struct fte *entry);

/* Initialise the frame system. Palloc must be initialised. */
void frame_init(void)
{
	/* Allocate memory for frame table entries */
	user_base = palloc_pool_base(true);
	size_t user_pool_size = palloc_pool_size(true);
	ftes = malloc(user_pool_size * sizeof(struct fte));

	if (!ftes)
		PANIC("Unable to allocate space from user pool frame table.");

	/* Initialise queue system. */
	lock_init(&used_queue_lock);
	sema_init(&unlocked_frames, user_pool_size);
	list_init(&used_queue);
}

/* Get a free frame from palloc, if all frames are used, evict a frame. */
void *frame_get(void)
{
	sema_down(&unlocked_frames);
	void *new_page = palloc_get_page(PAL_USER);

	if (new_page)
		return new_page;

	/* If there are no free frames available, must evict a page & replace.
	 * Run second chance algorithm, using USED_QUEUE as a circular queue.
	 * The frame_was_accessed() and frame_reset_accessed() functions check for
	 * access of mmaps (requires accessing multiple page directories),
	 * swappable frames.
	 */
	lock_acquire(&used_queue_lock);
	struct fte *evictee;
	void *page;
	struct list_elem *evictee_elem = list_pop_front(&used_queue);
	evictee = list_entry(evictee_elem, struct fte, used_elem);
	page = fte_to_kpage(evictee);

	while (frame_was_accessed(evictee)) {
		frame_reset_accessed(evictee);
		list_push_back(&used_queue, evictee_elem);

		evictee_elem = list_pop_front(&used_queue);
		evictee = list_entry(evictee_elem, struct fte, used_elem);
		page = fte_to_kpage(evictee);
	}

	/* Evict the frame, and reset ownership. */
	if (evictee->is_swappable) {
		uint32_t *pd = evictee->pd;
		void *vpage = evictee->vpage;

		frame_reset(evictee);

		swap_page_evict(page, pd, vpage, &used_queue_lock);
		ASSERT(!lock_held_by_current_thread(&used_queue_lock));
	} else {
		void *shared_mmap = evictee->shared_mmap;

		frame_reset(evictee);

		mmap_frame_evict(page, shared_mmap, &used_queue_lock);
	}

	/* Return the locked frame (cannot be evicted). */
	return page;
}

/* FRAME LOCKING:
 * frame identifier: *kpage (from the frame table entry)
 * frame owner:      shared_mmap for mmaps | pagedir for swappable pages
 *
 *   KPAGE + OWNER
 *         |
 *         |
 *  +------+-------+
 *  |  FRAME LOCK  |
 *  +-+----+-------+
 *    |    | SUCCESS (true)
 *    |    +---------> The frame is in memory, and cannot be evicted.
 *    |
 *    | FAILURE (false)
 *    +------> The frame is no longer present in memory (in swap or filesystem),
 *             it will not be altered by the frame system until the process
 *             (or other processes sharing) load it in.
 *
 * By using this frame lock, we can exclude the frame system from evicting the
 * frame.
 *
 * In the case of a swappable (e.g stack page) this ensures mutual exclusion
 * (only the process owning the frame can access it).
 *
 * In the case of an mmap, only processes sharing the frame can access it, and
 * do so through synchronisation provided in the mmaping system.
 */

/* Lock a frame containing an mmaped page. */
bool frame_lock_mmaped(struct shared_mmap *shared_mmap, void *kpage)
{
	/* We cannot optimize this SEMA_DOWN() away here because, by the time this
	 * function exits, it is crucial that the USED_QUEUE_LOCK has been released
	 * during the potential eviction in FRAME_GET() and that acquiring the
	 * lock for the specific page that we are evicting there will corelate the
	 * state that this function has returned and whether the page we were trying
	 * to lock is paged-in or paged-out.
	 * 
	 * For the specific reasons of this reliance in this function please refer
	 * to the comments in MMAP_UNREGISTER() and MMAP_FRAME_EVICT() in mmap.c
	 */
	sema_down(&unlocked_frames);
	lock_acquire(&used_queue_lock);
	struct fte *frame = kpage_to_fte(kpage);

	if (frame->is_swappable || frame->shared_mmap != shared_mmap) {
		lock_release(&used_queue_lock);

		/* Frame could not be locked, so restore UNLOCKED_FRAMES to previous
		 * state.
		 */
		sema_up(&unlocked_frames);
		return false;
	}

	list_remove(&frame->used_elem);
	frame_reset(frame);

	lock_release(&used_queue_lock);

	return true;
}

/* Lock a frame containing swappable page (e.g stack page). */
bool frame_lock_swappable(uint32_t *pd, void *vpage, void *kpage)
{
	/* We cannot optimize this SEMA_DOWN() here because, by the time this
	 * function exits, it is crucial that the USED_QUEUE_LOCK has been released
	 * during the potential eviction in FRAME_GET() and that acquiring the
	 * lock for the specific page that we are evicting there will corelate the
	 * state that this function has returned and whether the page we were trying
	 * to lock is paged-in or paged-out.
	 * 
	 * In this case, this has to do with the PAGEDIR_DESTROY() using this function
	 * to check whether the page has been evicted to swap or not and whether it
	 * can safely assume that, if this function fails, it can grab the swap id
	 * that the page that is being locked here has been evicted to.
	 */
	sema_down(&unlocked_frames);
	lock_acquire(&used_queue_lock);
	struct fte *frame = kpage_to_fte(kpage);

	if (!frame->is_swappable || frame->pd != pd || frame->vpage != vpage) {
		lock_release(&used_queue_lock);
		sema_up(&unlocked_frames);
		return false;
	}

	list_remove(&frame->used_elem);
	frame_reset(frame);

	lock_release(&used_queue_lock);

	return true;
}

/* Reset the frame by NULLifying the owner When a frame is not present,
 * is_swappable = false & shared_mmap = NULL.
 */
static void frame_reset(struct fte *entry)
{
	entry->is_swappable = false;
	entry->shared_mmap = NULL;
}

/* FRAME UNLOCKING:
 * When unlocking a frame we are adding it to the queue of frames that can
 * potentially be evicted by page replacement.
 *
 * When unlocking, the FTE is set to ensure the correct access checking,
 * resetting and eviction behaviour.
 */

/* Unlock a frame as an mmaped frame. */
void frame_unlock_mmaped(struct shared_mmap *shared_mmap, void *kpage)
{
	struct fte *frame = kpage_to_fte(kpage);

	frame->is_swappable = false;
	frame->shared_mmap = shared_mmap;

	lock_acquire(&used_queue_lock);
	list_push_front(&used_queue, &frame->used_elem);

	lock_release(&used_queue_lock);
	sema_up(&unlocked_frames);
}

/* Unlock a frame as a swappable frame. */
void frame_unlock_swappable(uint32_t *pd, void *vpage, void *kpage)
{
	struct fte *frame = kpage_to_fte(kpage);

	frame->is_swappable = true;
	frame->pd = pd;
	frame->vpage = vpage;

	lock_acquire(&used_queue_lock);
	list_push_front(&used_queue, &frame->used_elem);

	lock_release(&used_queue_lock);
	sema_up(&unlocked_frames);
}

/* Mark a page as freed. Frame must be frame locked. */
void frame_free(void *kpage)
{
	ASSERT(!kpage_to_fte(kpage)->is_swappable &&
				 !kpage_to_fte(kpage)->shared_mmap);
	palloc_free_page(kpage);
	sema_up(&unlocked_frames);
}

/* Get the fte from a given page. */
struct fte *kpage_to_fte(void *kpage)
{
	return &ftes[pg_no(kpage) - pg_no(user_base)];
}

/* Get the page represented by an fte. */
void *fte_to_kpage(struct fte *fte)
{
	return user_base + (fte - ftes) * PGSIZE;
}

/* Check if a frame has been accessed, delgating to the correct function for
 * the frame type.
 */
static inline bool frame_was_accessed(struct fte *entry)
{
	if (entry->is_swappable)
		return swap_page_was_accessed(entry->pd, entry->vpage);
	else
		return mmap_frame_was_accessed(entry->shared_mmap);
}

/* Reset the frame accesses, delgating to the correct function for
 * the frame type.
 */
static inline void frame_reset_accessed(struct fte *entry)
{
	if (entry->is_swappable)
		swap_page_reset_accessed(entry->pd, entry->vpage);
	else
		mmap_frame_reset_accessed(entry->shared_mmap);
}
