#include "vm/lazy.h"
#include <string.h>
#include "threads/malloc.h"
#include "userprog/syscall.h"
#include "threads/vaddr.h"

/* Page to be lazily loaded from disk, to become a swappable page. */
struct lazy_load {
	bool is_lazy; /* Should be set to true, used for dispatch from ptes */
	struct file *file; /* The file from which to lazy load from the disk */
	off_t file_offset; /* The offset from the file from which to read from */
	uint16_t length; /* Bytes to read from the file at offset. */
};

/* Creates a lazy loaded page, which will page-in as a normal page on
 * a page-fault, and then possibly be evictable to the swap rather than the
 * filesystem like it is done with mmaps.
 */
struct lazy_load *create_lazy_load(struct file *file, off_t file_offset,
																	 uint16_t length)
{
	struct lazy_load *lazy = malloc(sizeof(struct lazy_load));
	if (!lazy)
		return NULL;
	lazy->is_lazy = true;
	lazy->file = file_reopen(file);
	if (!lazy->file) {
		free(lazy);
		return NULL;
	}
	file_deny_write(lazy->file);
	lazy->file_offset = file_offset;
	lazy->length = length;
	return lazy;
}

/* Load the page from disk, freeing the associated lazy_load struct, can now be
 * used as a swappable.
 */
void lazy_load_lazy(void *kpage, struct lazy_load *lazy)
{
	ASSERT(lazy);
	ASSERT(pg_ofs(kpage) == 0);

	filesys_enter();
	file_seek(lazy->file, lazy->file_offset);
	file_read(lazy->file, kpage, lazy->length);
	file_close(lazy->file);
	filesys_exit();
	memset(kpage + lazy->length, 0, PGSIZE - lazy->length);
	free(lazy);
}

/* Free the lazy loading struct without loading it anywhere */
void lazy_free(struct lazy_load *lazy)
{
	filesys_enter();
	file_close(lazy->file);
	filesys_exit();
	free(lazy);
}
