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

/* Take an argument of a type from the pointer, then increment.
 * To be used to get arguments from syscall interrupt frame's stack
 */

#define GET_ARG(ptr, type, name)                                               \
	type name;                                                                   \
	if (!check_read_user_buffer(ptr, sizeof(type)))                              \
		thread_exit();                                                             \
	memcpy(&name, ptr, sizeof(type));                                            \
	ptr += sizeof(void *)

#define RETURN(ptr, val) *ret = val

#define PAGESTART(ptr) (((uintptr_t)ptr / PGSIZE) * PGSIZE)

typedef void sys_handle(uint32_t *, const void *);

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

static void syscall_handler(struct intr_frame *);

static sys_handle *syscall_handlers[NUM_SYSCALL];

/* Safe access to user memory */
bool check_read_user_buffer(const void *buffer_, unsigned size);
bool check_write_user_buffer(void *buffer_, unsigned size);
static bool check_user_string(const char *str);
static int get_user(const uint8_t *uaddr);
static bool put_user(uint8_t *udst, uint8_t byte);

static inline size_t fd_to_idx(unsigned fd);
static inline unsigned idx_to_fd(size_t idx);
static inline struct file *get_file(size_t idx);

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

	/* Initialize global filesystem lock */
	lock_init(&filesys_lock);
}

static void syscall_handler(struct intr_frame *f)
{
	/* Get the args from the stack. */
	if (!check_read_user_buffer(f->esp, sizeof(int)))
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
bool check_read_user_buffer(const void *buffer_, unsigned size)
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

/* Checks if the buffer BUFFER of size SIZE can be safely written to
 * by the kernel.
 */
bool check_write_user_buffer(void *buffer_, unsigned size)
{
	uint8_t *buffer = buffer_;
	if (!size)
		return true;
	if (!is_user_vaddr(buffer + size - 1))
		return false;
	for (uint8_t *page = (uint8_t *)PAGESTART(buffer); page <= buffer;
			 page += PGSIZE)
		if (!put_user(page, get_user((void *)page)))
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

/* Convert a file descriptor FD to an index required to get an open file from a
 * thread.
 */
size_t fd_to_idx(unsigned fd)
{
	ASSERT(fd != STDOUT_FILENO && fd != STDIN_FILENO);
	size_t idx = (size_t)fd;

	if (fd > STDOUT_FILENO)
		idx--;
	if (fd > STDIN_FILENO)
		idx--;

	return idx;
}

/* Convert an index IDX (for file in thread's open files vector) into a file
 * descriptor.
 */
unsigned idx_to_fd(size_t idx)
{
	unsigned fd = (unsigned)idx;

	if (idx <= STDOUT_FILENO)
		fd++;
	if (idx <= STDIN_FILENO)
		fd++;

	return fd;
}

/* Returns the file corresponding to index IDX in the thread's open files. */
struct file *get_file(unsigned fd)
{
	size_t idx = fd_to_idx(fd);
	struct thread *cur = thread_current();
	if (idx >= bitmap_size(cur->open_files_bitmap) ||
			!bitmap_test(cur->open_files_bitmap, idx))
		return NULL;
	return vector_get(&cur->open_files, idx);
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
		RETURN(ret, -1);
		return;
	}

	struct thread *cur = thread_current();

	size_t idx = bitmap_first(cur->open_files_bitmap, false);

	if (idx == BITMAP_ERROR) {
		if (bitmap_push_back(cur->open_files_bitmap, true)) {
			if (vector_push_back(&cur->open_files, file)) {
				RETURN(ret, idx_to_fd(bitmap_size(cur->open_files_bitmap) - 1));
				return;
			}
			vector_pop_back(&cur->open_files);
		}
		filesys_enter();
		file_close(file);
		filesys_exit();
		RETURN(ret, -1);
	} else {
		vector_set(&cur->open_files, idx, file);
		bitmap_set(cur->open_files_bitmap, idx, true);
		RETURN(ret, idx_to_fd(idx));
	}
}

/* Returns the size of the file with descriptor FD. */
void filesize(uint32_t *ret, const void *args)
{
	GET_ARG(args, int, fd);
	if (fd == STDIN_FILENO || fd == STDOUT_FILENO) {
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
	if (fd == STDOUT_FILENO) {
		RETURN(ret, -1);
	} else if (fd == STDIN_FILENO) {
		for (size_t offset = 0; offset < size; offset++)
			((char *)buffer)[offset] = input_getc();
		RETURN(ret, size);
	} else {
		struct file *file = get_file(fd);
		if (!file) {
			RETURN(ret, -1);
			return;
		}

		if (!check_write_user_buffer(buffer, size))
			thread_exit();
		filesys_enter();
		RETURN(ret, file_read(file, buffer, size));
		filesys_exit();
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
	if (fd == STDIN_FILENO) {
		RETURN(ret, -1);
	} else if (fd == STDOUT_FILENO) {
		putbuf(buffer, size);
		RETURN(ret, size);
	} else {
		struct file *file = get_file(fd);
		if (!file) {
			RETURN(ret, -1);
			return;
		}

		if (!check_read_user_buffer(buffer, size))
			thread_exit();
		filesys_enter();
		RETURN(ret, file_write(file, buffer, size));
		filesys_exit();
	}
}

/* Sets the position to read and write from in open file with descriptor FD to
 * POSITION. Can seek beyond the end of the file.
 */
void seek(uint32_t *ret UNUSED, const void *args)
{
	GET_ARG(args, unsigned, fd);
	GET_ARG(args, unsigned, position);
	if (fd == STDIN_FILENO || fd == STDOUT_FILENO)
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
	if (fd == STDIN_FILENO || fd == STDOUT_FILENO) {
		RETURN(ret, -1);
	} else {
		struct file *file = get_file(fd);
		if (!file) {
			RETURN(ret, -1);
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
	if (fd == STDIN_FILENO || fd == STDOUT_FILENO)
		thread_exit();

	size_t idx = fd_to_idx(fd);
	struct thread *cur = thread_current();
	struct bitmap *b = cur->open_files_bitmap;

	if (idx >= bitmap_size(b))
		thread_exit();

	if (bitmap_test(b, idx)) {
		bitmap_set(b, idx, false);
		filesys_enter();
		file_close(vector_get(&cur->open_files, idx));
		filesys_exit();
	}
}
