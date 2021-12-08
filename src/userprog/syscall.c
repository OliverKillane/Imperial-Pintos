#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "threads/palloc.h"

#ifdef VM

#include "userprog/pagedir.h"
#include "vm/mmap.h"
#include "threads/malloc.h"

#define MAP_FAILED (-1)

#endif

#define FILE_FAILED (-1)

/* Take an argument of a type from the pointer, then increment.
 * To be used to get arguments from syscall interrupt frame's stack
 */
#define GET_ARG(ptr, type, name)                                               \
	type name;                                                                   \
	if (!check_user_buffer(ptr, sizeof(type)))                                   \
		thread_exit();                                                             \
	memcpy(&name, ptr, sizeof(type));                                            \
	ptr += sizeof(void *)

#define RETURN(ptr, val) *ret = val

#define PAGESTART(ptr) (((uintptr_t)ptr / PGSIZE) * PGSIZE)

typedef void sys_handle(uint32_t *, const void *);

/* intermediate page buffer for reading and writing to files */
int8_t *read_write_buffer;
struct lock read_write_buffer_lock;

/* Global lock for accessing files */
struct lock filesys_lock;

/* Lock file on entry */
void filesys_enter(void)
{
	lock_acquire(&filesys_lock);
}

/* Unlock file on exit */
void filesys_exit(void)
{
	lock_release(&filesys_lock);
}

/* Syscall handling subroutines. */
static sys_handle halt;
static sys_handle exit;
static sys_handle exec;
static sys_handle wait;
static sys_handle create;
static sys_handle remove;
static sys_handle open;
static sys_handle filesize;
static sys_handle read;
static sys_handle write;
static sys_handle seek;
static sys_handle tell;
static sys_handle close;

#ifdef VM

static sys_handle mmap;
static sys_handle munmap;

/* Helper function for failures in mmap and for destroying in munmap. */
static void unregister_all_mmaps(struct list *user_mmaps);

typedef int mapid_t;

#endif

static void syscall_handler(struct intr_frame *);

static sys_handle *syscall_handlers[NUM_SYSCALL];

/* Safe access to user memory */
bool check_user_buffer(const void *buffer_, unsigned size);
bool check_write_user_buffer(void *buffer_, unsigned size);
static bool check_user_string(const char *str);
static int get_user(const uint8_t *uaddr);
static bool put_user(uint8_t *udst, uint8_t byte);

static inline struct file *get_file(unsigned fd);

void syscall_init(void)
{
	/* Register the syscall handler for interrupts. */
	intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");

	/* Register syscall handlers. */
	syscall_handlers[SYS_HALT] = halt;
	syscall_handlers[SYS_EXIT] = exit;
	syscall_handlers[SYS_EXEC] = exec;
	syscall_handlers[SYS_WAIT] = wait;
	syscall_handlers[SYS_CREATE] = create;
	syscall_handlers[SYS_REMOVE] = remove;
	syscall_handlers[SYS_OPEN] = open;
	syscall_handlers[SYS_FILESIZE] = filesize;
	syscall_handlers[SYS_READ] = read;
	syscall_handlers[SYS_WRITE] = write;
	syscall_handlers[SYS_SEEK] = seek;
	syscall_handlers[SYS_TELL] = tell;
	syscall_handlers[SYS_CLOSE] = close;

#ifdef VM
	syscall_handlers[SYS_MMAP] = mmap;
	syscall_handlers[SYS_MUNMAP] = munmap;
#endif

	/* Initialize global filesystem lock */
	lock_init(&filesys_lock);

	/* Palloc the buffer used to copy data */
	read_write_buffer = palloc_get_page(0);
	if (!read_write_buffer)
		PANIC("Failed to palloc a read/write buffer for filesys");
	lock_init(&read_write_buffer_lock);
}

static void syscall_handler(struct intr_frame *f)
{
	/* Get the args from the stack. */
	if (!check_user_buffer(f->esp, sizeof(int)))
		thread_exit();
	int syscall_ref = *(int *)(f->esp);

	if (syscall_ref < 0 || syscall_ref >= NUM_SYSCALL)
		thread_exit();

	sys_handle *handler = syscall_handlers[syscall_ref];
	handler(&f->eax, f->esp + sizeof(void *));
}

/* Checks if the buffer BUFFER of size SIZE can be safely accessed
 * by the kernel.
 */
bool check_user_buffer(const void *buffer_, unsigned size)
{
	const uint8_t *buffer = buffer_;
	if (!size)
		return true;
	if (!is_user_vaddr(buffer + size - 1))
		return false;
	for (uint8_t *page = (uint8_t *)PAGESTART(buffer); page <= buffer;
			 page += PGSIZE)
		if (get_user((void *)page) == -1)
			return false;
	return true;
}

/* Checks if the string SRC provided by the user is in a valid memory. */
bool check_user_string(const char *src)
{
	if (!is_user_vaddr(src) || get_user((void *)src) == -1)
		return false;
	else if (*src)
		do {
			src++;
			if ((uintptr_t)src % PGSIZE &&
					(!is_user_vaddr(src) || get_user((void *)src) == -1))
				return false;
		} while (*src);
	return true;
}

/* Reads a byte at user virtual address UADDR.
 * UADDR must be below PHYS_BASE.
 * Returns the byte value if successful, -1 if a segfault
 * occurred.
 */
int get_user(const uint8_t *uaddr)
{
#ifndef NDEBUG
	thread_current()->may_page_fault = true;
#endif
	int result;
	asm("movl $1f, %0; movzbl %1, %0; 1:" : "=&a"(result) : "m"(*uaddr));
#ifndef NDEBUG
	thread_current()->may_page_fault = false;
#endif
	return result;
}

/* Writes BYTE to user address UDST.
 * UDST must be below PHYS_BASE.
 * Returns true if successful, false if a segfault occurred.
 */
bool put_user(uint8_t *udst, uint8_t byte)
{
#ifndef NDEBUG
	thread_current()->may_page_fault = true;
#endif
	int error_code;
	asm("movl $1f, %0; movb %b2, %1; 1:"
			: "=&a"(error_code), "=m"(*udst)
			: "q"(byte));
#ifndef NDEBUG
	thread_current()->may_page_fault = false;
#endif
	return error_code != -1;
}

/* Start of valid file descriptors */
#if STDIN_FILENO < STDOUT_FILENO
#define FD_START (STDOUT_FILENO + 1)
#else
#define FD_START (STDIN_FILENO + 1)
#endif

/* Returns the file corresponding to index IDX in the thread's open files. */
struct file *get_file(unsigned fd)
{
	size_t idx = fd - FD_START;
	struct vector *open_files = &thread_current()->open_files;

	if (idx >= vector_size(open_files) || !vector_get(open_files, idx))
		return NULL;
	return vector_get(open_files, idx);
}

/* Terminates Pintos by calling shutdown_power_off(). */
void halt(uint32_t *ret UNUSED, const void *args UNUSED)
{
	shutdown_power_off();
}

/* Terminates the current user program, sending its exit status STATUS to the
 * kernel. If the process's parent waits for it (see below), this is the status
 * that will be returned.
 */
void exit(uint32_t *ret UNUSED, const void *args)
{
	GET_ARG(args, int, status);
	process_set_exit_status(status);
	thread_exit();
}

/* Executes the COMMAND provided. with arguments, returning the process ID. */
void exec(uint32_t *ret, const void *args)
{
	GET_ARG(args, char *, command);
	if (check_user_string(command))
		RETURN(ret, process_execute(command));
	else
		thread_exit();
}

/* Waits for the process PID and retrieves the exit status once it terminates.
 * Note that the process PID may have terminated before this function is run.
 * Returns the exit status of the process.
 */
void wait(uint32_t *ret, const void *args)
{
	GET_ARG(args, pid_t, pid);
	RETURN(ret, process_wait(pid));
}

/* Creates a new file FILENAME of INITIAL_SIZE bytes. Name cannot be
 * an empty string (no file created). Returns true if file was created.
 */
void create(uint32_t *ret, const void *args)
{
	GET_ARG(args, char *, filename);
	GET_ARG(args, unsigned, initial_size);
	if (!check_user_string(filename)) {
		thread_exit();
	} else if (!*filename) {
		RETURN(ret, false);
	} else {
		filesys_enter();
		RETURN(ret, filesys_create(filename, initial_size));
		filesys_exit();
	}
}

/* Deletes the file FILENAME. */
void remove(uint32_t *ret, const void *args)
{
	GET_ARG(args, char *, filename);
	if (!check_user_string(filename))
		thread_exit();
	filesys_enter();
	RETURN(ret, filesys_remove(filename));
	filesys_exit();
}

/* Opens the file FILENAME. Returning a file descriptor for the file. */
void open(uint32_t *ret, const void *args)
{
	GET_ARG(args, char *, filename);
	if (!check_user_string(filename))
		thread_exit();

	filesys_enter();
	struct file *file = filesys_open(filename);
	filesys_exit();
	if (!file) {
		RETURN(ret, FILE_FAILED);
		return;
	}

	struct vector *open_files = &thread_current()->open_files;
	size_t offset = 0;

	while (offset < vector_size(open_files) && vector_get(open_files, offset))
		offset++;

	if (offset == vector_size(open_files)) {
		if (!vector_push_back(open_files, file)) {
			filesys_enter();
			file_close(file);
			filesys_exit();
			RETURN(ret, FILE_FAILED);
			return;
		}
	} else {
		vector_set(open_files, offset, file);
	}

	RETURN(ret, offset + FD_START);
}

/* Returns the size of the file with descriptor FD. */
void filesize(uint32_t *ret, const void *args)
{
	GET_ARG(args, int, fd);
	if (fd < FD_START) {
		RETURN(ret, 0);
		return;
	}

	struct file *file = get_file(fd);
	if (!file) {
		RETURN(ret, 0);
		return;
	}

	filesys_enter();
	RETURN(ret, file_length(file));
	filesys_exit();
}

/* Reads SIZE bytes from a file with descriptor FD into buffer BUFFER.
 * Returns the number of bytes read (e.g 0 at EOF), or -1 if the file could not
 * be read for another reason.
 */
void read(uint32_t *ret, const void *args)
{
	GET_ARG(args, unsigned, fd);
	GET_ARG(args, void *, buffer);
	GET_ARG(args, unsigned, size);
	if (!is_user_vaddr(buffer + size - 1))
		thread_exit();

	if ((off_t)size < 0) {
		RETURN(ret, FILE_FAILED);
	} else if (fd < FD_START && fd != STDIN_FILENO) {
		RETURN(ret, FILE_FAILED);
	} else if (fd == STDIN_FILENO) {
		for (size_t offset = 0; offset < size; offset++) {
			if (!put_user(buffer + offset, input_getc()))
				thread_exit();
		}
		RETURN(ret, size);
	} else {
		struct file *file = get_file(fd);
		if (!file) {
			RETURN(ret, FILE_FAILED);
			return;
		}

		off_t total_bytes_read = 0;
		do {
			lock_acquire(&read_write_buffer_lock);
			off_t bytes_to_read = (total_bytes_read + PGSIZE <= (off_t)size) ?
																		PGSIZE :
																		(size - total_bytes_read);
			filesys_enter();
			off_t bytes_read = file_read(file, read_write_buffer, bytes_to_read);
			filesys_exit();
			for (off_t i = 0; i < bytes_read; i++) {
				if (!put_user(buffer + total_bytes_read + i,
											*(read_write_buffer + i))) {
					lock_release(&read_write_buffer_lock);
					thread_exit();
				}
			}
			lock_release(&read_write_buffer_lock);
			total_bytes_read += bytes_read;
			if (bytes_read < bytes_to_read)
				break;
		} while (total_bytes_read < (off_t)size);

		RETURN(ret, total_bytes_read);
	}
}

/* Writes SIZE bytes from buffer BUFFER into the open file with descriptor FD.
 * If fd = 1 writes to the console. Returns The number of bytes written (e.g 0
 * at EOF).
 */
void write(uint32_t *ret, const void *args)
{
	GET_ARG(args, int, fd);
	GET_ARG(args, void *, buffer);
	GET_ARG(args, unsigned, size);

	if (!is_user_vaddr(buffer + size - 1))
		thread_exit();

	if (fd < FD_START && fd != STDOUT_FILENO) {
		RETURN(ret, FILE_FAILED);
	} else {
		struct file *file = NULL;
		if (fd != STDOUT_FILENO) {
			file = get_file(fd);
			if (!file) {
				RETURN(ret, FILE_FAILED);
				return;
			}
		}

		off_t total_bytes_written = 0;
		do {
			off_t bytes_to_write = (total_bytes_written + PGSIZE <= (off_t)size) ?
																		 PGSIZE :
																		 (size - total_bytes_written);
			lock_acquire(&read_write_buffer_lock);
			for (off_t i = 0; i < bytes_to_write; i++) {
				int byte_read = get_user(buffer + total_bytes_written + i);
				if (byte_read == -1) {
					lock_release(&read_write_buffer_lock);
					thread_exit();
				}
				*(read_write_buffer + i) = byte_read;
			}
			off_t bytes_written;
			if (fd == STDOUT_FILENO) {
				putbuf((int8_t *)read_write_buffer, bytes_to_write);
				bytes_written = bytes_to_write;
			} else {
				filesys_enter();
				bytes_written = file_write(file, read_write_buffer, bytes_to_write);
				filesys_exit();
			}
			lock_release(&read_write_buffer_lock);
			total_bytes_written += bytes_written;
			if (bytes_written < bytes_to_write)
				break;
		} while (total_bytes_written < (off_t)size);

		RETURN(ret, total_bytes_written);
	}
}

/* Sets the position to read and write from in open file with descriptor FD to
 * POSITION. Can seek beyond the end of the file.
 */
void seek(uint32_t *ret UNUSED, const void *args)
{
	GET_ARG(args, unsigned, fd);
	GET_ARG(args, unsigned, position);
	if (fd < FD_START)
		return;

	struct file *file = get_file(fd);
	if (!file)
		return;

	filesys_enter();
	file_seek(file, position);
	filesys_exit();
}

/* Gets the position of the next byte to be read in open file with descriptor
 * FD.
 */
void tell(uint32_t *ret, const void *args)
{
	GET_ARG(args, unsigned, fd);
	if (fd < FD_START) {
		RETURN(ret, FILE_FAILED);
	} else {
		struct file *file = get_file(fd);
		if (!file) {
			RETURN(ret, FILE_FAILED);
			return;
		}

		filesys_enter();
		RETURN(ret, file_tell(file));
		filesys_exit();
	}
}

/* Closes the open file with descriptor FD. When a process exits all a process'
 * open files are closed.
 */
void close(uint32_t *ret UNUSED, const void *args)
{
	GET_ARG(args, unsigned, fd);
	if (fd < FD_START)
		thread_exit();

	size_t idx = fd - FD_START;
	struct vector *open_files = &thread_current()->open_files;

	if (idx >= vector_size(open_files) || !vector_get(open_files, idx))
		thread_exit();

	filesys_enter();
	file_close(vector_get(open_files, idx));
	filesys_exit();
	vector_set(open_files, idx, NULL);
}

#ifdef VM

/* Task 3 VM syscalls */

/* Memory maps the open file with file descriptor FD to pages starting at
 * address ADDR.
 *
 * Fails if:
 * - fd is STDIN or STDOUT.
 * - fd is not a valid open file.
 * - address is not page aligned.
 * - page being mapped has already been set.
 * - address is zero.
 */
void mmap(uint32_t *ret, const void *args)
{
	GET_ARG(args, unsigned, fd);
	GET_ARG(args, void *, addr);

	/* Fail if file descriptor is invalid, or address not page aligned. */
	if (fd < FD_START || !addr || pg_ofs(addr) != 0) {
		RETURN(ret, MAP_FAILED);
		return;
	}

	/* Fail if file invalid. */
	struct file *file_to_map = get_file(fd);
	if (!file_to_map) {
		RETURN(ret, MAP_FAILED);
		return;
	}

	/* Fail if file length is zero. */
	off_t length = file_length(file_to_map);
	if (!length) {
		RETURN(ret, MAP_FAILED);
		return;
	}

	/* fail if unable to create a new list to contain the mapped pages. */
	struct list *user_mmap_pages = malloc(sizeof(struct list));
	if (!user_mmap_pages) {
		RETURN(ret, MAP_FAILED);
		return;
	}
	list_init(user_mmap_pages);

	struct thread *cur = thread_current();

	mapid_t map_id = 0;
	while (map_id < (mapid_t)vector_size(&cur->mmapings) &&
				 vector_get(&cur->mmapings, map_id))
		map_id++;

	if (map_id == (mapid_t)vector_size(&cur->mmapings) &&
			!vector_push_back(&cur->mmapings, NULL)) {
		free(user_mmap_pages);
		RETURN(ret, MAP_FAILED);
		return;
	}

	/* load each page, if any page overlaps an already set page, or is  */
	for (off_t offset = 0; offset < length; offset += PGSIZE, addr += PGSIZE) {
		if (pagedir_get_page_type(cur->pagedir, addr) == NOTSET) {
			uint16_t size = offset + PGSIZE > length ? length - offset : PGSIZE;
			if (!mmap_register(file_to_map, offset, size, true, cur->pagedir, addr,
												 user_mmap_pages)) {
				unregister_all_mmaps(user_mmap_pages);
				RETURN(ret, MAP_FAILED);
				return;
			}
		} else {
			unregister_all_mmaps(user_mmap_pages);
			RETURN(ret, MAP_FAILED);
			return;
		}
	}
	vector_set(&cur->mmapings, map_id, user_mmap_pages);
	RETURN(ret, map_id);
}

/* Unmap a memory mapped file. If there is no mmap for that MMAPD_ID then
 * terminate the process.
 */
void munmap(uint32_t *ret UNUSED, const void *args)
{
	GET_ARG(args, mapid_t, mmap_id);

	struct vector *mmapings = &thread_current()->mmapings;
	if (mmap_id >= (mapid_t)vector_size(mmapings))
		thread_exit();

	struct list *user_mmap_pages = vector_get(mmapings, mmap_id);
	if (!user_mmap_pages)
		thread_exit();

	unregister_all_mmaps(user_mmap_pages);
	vector_set(mmapings, mmap_id, NULL);
}

/* Unregister all user_mmaped pages from the list USER_MMAPS
 * and free the list.
 */
static void unregister_all_mmaps(struct list *user_mmaps)
{
	while (!list_empty(user_mmaps))
		mmap_unregister(mmap_list_entry(list_front(user_mmaps)));
	free(user_mmaps);
}
#endif
