
#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "userprog/tss.h"

/* The initial size of the bitmap of open files */
#define OPEN_FILES_SIZE 4

/* Default exit status on error */
#define EXIT_STATUS_ERR -1

/* Setup struct for information to the child process */
struct process_info {
	uint8_t *stack_template;
	uint16_t stack_size;
	struct child_manager *parent;
};

static thread_func start_process NO_RETURN;
static bool load(void (**eip)(void), char *file_name);
static inline bool test_and_set(bool *flag);
static inline uintptr_t word_align(uintptr_t ptr);
static bool setup_stack(void **esp, uint16_t stack_size, uint8_t *kpage);
static bool stack_page_setup(struct process_info *child_data);
// static bool child_data_setup(struct process_info *child_data);
static bool strtok_setup(const char *command, char **command_tok_ptr);
static void put_string_on_stack(struct process_info *child_data, int *argc,
																char **argv, const char *token);
static bool populate_stack(struct process_info *child_data,
													 const char *file_name_str, char *tok_save_ptr);

bool stack_page_setup(struct process_info *child_data)
{
	child_data->stack_template = palloc_get_page(PAL_USER);
	if (!child_data->stack_template)
		return false;
	child_data->stack_size = 0;

	return true;
}

bool strtok_setup(const char *command, char **command_tok_ptr)
{
	size_t command_len = strlen(command) + 1;
	(*command_tok_ptr) = malloc(command_len);
	char *command_tok = *command_tok_ptr;
	if (!command_tok)
		return false;
	strlcpy(command_tok, command, command_len);
	return true;
}

void put_string_on_stack(struct process_info *child_data, int *argc,
												 char **argv, const char *token)
{
	/* Putting the data for the main() and data for argv argument for it in */
	child_data->stack_size +=
					strlcpy((char *)child_data->stack_template + PGSIZE -
													child_data->stack_size - strlen(token) - 1,
									token, PGSIZE) +
					1;
	argv[(*argc)++] = PHYS_BASE - child_data->stack_size;
}

bool populate_stack(struct process_info *child_data, const char *file_name_str,
										char *tok_save_ptr)
{
	/* Argv array data */
	int argc = 0;
	char **const argv = (char **)(child_data->stack_template + sizeof(char **) +
																sizeof(int) + sizeof(void (*)(void)));

	/* Check if the page has at least enough size to accommodate one token of
	 * size 0.
	 */
	_Static_assert(PGSIZE >= 15 + sizeof(char **) + sizeof(int) +
																	 sizeof(void (*)(void)),
								 "Page size must be at least 23");

	/* Putting in the filename data for the main() and data for argc argument
	 * for it in
	 */
	put_string_on_stack(child_data, &argc, argv, file_name_str);

	for (char *token = strtok_r(NULL, " ", &tok_save_ptr); token;
			 token = strtok_r(NULL, " ", &tok_save_ptr)) {
		/* Checking if we're still fitting in the page */
		if (PGSIZE -
								word_align(PGSIZE - child_data->stack_size - strlen(token) -
													 1) +
								((uint8_t *)&argv[argc + 3] - child_data->stack_template) >=
				PGSIZE) {
			return false;
		}

		/* Putting the data for the main() and data for argv argument for it in */
		put_string_on_stack(child_data, &argc, argv, token);
	}

	/* Word-aligning the data for the main() */
	uint8_t word_align_offset = PGSIZE - child_data->stack_size -
															word_align(PGSIZE - child_data->stack_size);
	memset(child_data->stack_template + PGSIZE - child_data->stack_size -
								 word_align_offset,
				 0, word_align_offset);
	child_data->stack_size += word_align_offset;

	/* Setting the call frame for the main() */
	argv[argc] = 0;
	*((char **)(child_data->stack_template + sizeof(int) +
							sizeof(void (*)(void)))) =
					PHYS_BASE - child_data->stack_size - (argc + 1) * sizeof(char *);
	*((int *)(child_data->stack_template + sizeof(void (*)(void)))) = argc;
	*((void (**)(void))child_data->stack_template) = NULL;

	/* Moving the arguments to main() to the proper spot */
	memmove(child_data->stack_template + PGSIZE - child_data->stack_size -
									((uint8_t *)&argv[argc + 1] - child_data->stack_template),
					child_data->stack_template,
					(uint8_t *)&argv[argc + 1] - child_data->stack_template);

	child_data->stack_size +=
					(uint8_t *)&argv[argc + 1] - child_data->stack_template;

	return true;
}

/* Starts a new thread running a user program loaded from
 * FILENAME.  The new thread may be scheduled (and may even exit)
 * before process_execute() returns.  Returns the new process's
 * thread id, or TID_ERROR if the thread cannot be created.
 */
tid_t process_execute(const char *command)
{
	char *command_tok, *file_name_str;

	/* Setting the data that will be passed into start_process up. */
	struct process_info child_data;
	child_data.parent = malloc(sizeof(struct child_manager));
	if (child_data.parent) {
		child_data.parent->exit_status = EXIT_STATUS_ERR;
		child_data.parent->release = 0;
		child_data.parent->tid = TID_ERROR;
		sema_init(&child_data.parent->wait_sema, 0);
		list_push_back(&thread_current()->children, &child_data.parent->elem);

		/* Setting the child pointer up */
		if (stack_page_setup(&child_data)) {
			/* Copying the filename for tokenization. */
			if (strtok_setup(command, &command_tok)) {
				/* Getting the actual file name for the process */

				char *tok_save_ptr;
				file_name_str = strtok_r(command_tok, " ", &tok_save_ptr);
				ASSERT(file_name_str);

				if (strlen(file_name_str) <= 14) {
					if (populate_stack(&child_data, file_name_str, tok_save_ptr)) {
						/* We need to save the pointer to the child for later */
						struct child_manager *child = child_data.parent;

						/* Create a new thread to execute the process. */

						tid_t tid;
						tid = thread_create(file_name_str, PRI_DEFAULT, start_process,
																&child_data);
						if (tid != TID_ERROR) {
							free(command_tok);
							sema_down(&child->wait_sema);

							/* In case the child has failed to be loaded */
							if (child->tid == TID_ERROR) {
								tid = TID_ERROR;
								list_remove(&child->elem);
								if (test_and_set(&child->release))
									free(child);
							}
							return tid;
						}
					}
				}
				free(command_tok);
			}
			palloc_free_page(child_data.stack_template);
		}
	}
	free(child_data.parent);
	return TID_ERROR;
}

/* A thread function that loads a user process and starts it
 * running.
 */
static void start_process(void *data_ptr)
{
	struct process_info *data = data_ptr;
#ifndef NDEBUG
	thread_current()->may_page_fault = false;
#endif
	/* Initialize interrupt frame and load executable. */
	struct intr_frame if_;
	memset(&if_, 0, sizeof if_);
	if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
	if_.cs = SEL_UCSEG;
	if_.eflags = FLAG_IF | FLAG_MBS;

	char **file_name_user_ptr =
					*(char ***)(data->stack_template + PGSIZE - data->stack_size +
											sizeof(void (*)(void)) + sizeof(int) + sizeof(char **));
	char *file_name =
					(char *)(data->stack_template + PGSIZE -
									 ((uint8_t *)PHYS_BASE - (uint8_t *)file_name_user_ptr));

	bool success = load(&if_.eip, file_name);
	/* Attempt to set up the user stack. If it fails, then we free the
	 * stack template.
	 */
	if (!setup_stack(&if_.esp, data->stack_size, data->stack_template)) {
		palloc_free_page(data->stack_template);
		success = false;
	}

	/* Initializing the process data. This has to be done before informing the
	 * process about the success, since the data is allocated in the stack frame
	 * of process_execute
	 */
	thread_current()->parent = data->parent;
	vector_init(&thread_current()->open_files);
	thread_current()->open_files_bitmap = bitmap_create(0);

	/* Informing the process_execute that we have set our new tid. */
	if (success)
		data->parent->tid = thread_current()->tid;
	sema_up(&data->parent->wait_sema); /* DATA is not usable past this point */

	/* If load failed, free the data and exit the thread. */
	if (!success) {
		/* If the page directory failed to be assigned,
		 * then process resource won't be freed by process_exit.
		 */
		if (!thread_current()->pagedir) {
			printf("%s: exit(%d)\n", thread_current()->name, EXIT_STATUS_ERR);
			sema_up(&thread_current()->parent->wait_sema);
			/* If the parent thread has already exited, then free the
			 * child_manager. Test and set allows a read and then write to be
			 * atomic.
			 */
			if (test_and_set(&thread_current()->parent->release))
				free(thread_current()->parent);
		}
		thread_exit();
	}

	/* Start the user process by simulating a return from an
	 * interrupt, implemented by intr_exit (in
	 * threads/intr-stubs.S).  Because intr_exit takes all of its
	 * arguments on the stack in the form of a `struct intr_frame',
	 * we just point the stack pointer (%esp) to our stack frame
	 * and jump to it.
	 */
	asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
	NOT_REACHED();
}

/* Waits for thread child thread CHILD_TID to die and returns its exit status.
 * If it was terminated by the kernel (i.e. killed due to an exception),
 * returns -1. If child thread's tid is invalid or if it was not a child of the
 * calling process, or if process_wait() has already been successfully called
 * for the given child, returns -1 immediately, without waiting.
 */
int process_wait(tid_t child_tid)
{
	struct list *children = &thread_current()->children;
	struct child_manager *child = NULL;
	for (struct list_elem *e = list_begin(children); e != list_end(children);
			 e = list_next(e)) {
		if (list_entry(e, struct child_manager, elem)->tid == child_tid) {
			child = list_entry(e, struct child_manager, elem);
			break;
		}
	}
	if (!child)
		return -1;
	sema_down(&child->wait_sema);
	int exit_status = child->exit_status;
	list_remove(&child->elem);
	if (test_and_set(&child->release))
		free(child);
	return exit_status;
}

/* Free the current process's resources.
 * The synchronization of freeing the child and parent thread manager is done
 * using an atomic test_and_set function, which atomically gets the value of
 * the boolean variable and sets it to true. This way, we can count the number
 * of release attempts on the manager and free it on the second attempt, no
 * matter which side it comes from.
 */
void process_exit(void)
{
	struct thread *cur = thread_current();
	uint32_t *pd;

	/* Free the children managers of the thread */
	while (!list_empty(&cur->children)) {
		struct child_manager *child = list_entry(list_pop_front(&cur->children),
																						 struct child_manager, elem);
		if (test_and_set(&child->release))
			free(child);
	}

	/* Destroy the current process's page directory and its resources and
	 * switch back to the kernel-only page directory.
	 */
	pd = cur->pagedir;
	if (pd) {
		printf("%s: exit(%d)\n", cur->name, cur->parent->exit_status);

		/* Stop the parent from waiting */
		sema_up(&cur->parent->wait_sema);

		/* Free the parent manager of the thread */
		if (test_and_set(&cur->parent->release))
			free(cur->parent);

		/* Close the open files */
		for (size_t offset = 0; offset < vector_size(&cur->open_files); offset++)
			if (bitmap_test(cur->open_files_bitmap, offset)) {
				filesys_enter();
				file_close(vector_get(&cur->open_files, offset));
				filesys_exit();
			}

		/* Free the file management system */
		bitmap_destroy(cur->open_files_bitmap);
		vector_destroy(&cur->open_files);

		/* Close the code file */
		filesys_enter();
		file_close(cur->exec_file);
		filesys_exit();

		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared).
		 */
		cur->pagedir = NULL;
		pagedir_activate(NULL);
		pagedir_destroy(pd);
	}
}

/* Sets up the CPU for running user code in the current
 * thread.
 * This function is called on every context switch.
 */
void process_activate(void)
{
	struct thread *t = thread_current();

	/* Activate thread's page tables. */
	pagedir_activate(t->pagedir);

	/* Set thread's kernel stack for use in processing
	 * interrupts.
	 */
	tss_update();
}

/* Sets the exit code of the current process, which will then be returned
 * with a wait syscall.
 */
void process_set_exit_status(int status)
{
	thread_current()->parent->exit_status = status;
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.
 */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32 /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32 /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32 /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16 /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary.
 */
struct Elf32_Ehdr {
	unsigned char e_ident[16];
	Elf32_Half e_type;
	Elf32_Half e_machine;
	Elf32_Word e_version;
	Elf32_Addr e_entry;
	Elf32_Off e_phoff;
	Elf32_Off e_shoff;
	Elf32_Word e_flags;
	Elf32_Half e_ehsize;
	Elf32_Half e_phentsize;
	Elf32_Half e_phnum;
	Elf32_Half e_shentsize;
	Elf32_Half e_shnum;
	Elf32_Half e_shstrndx;
};

/* Program header.  See [ELF1] 2-2 to 2-4.
 * There are e_phnum of these, starting at file offset e_phoff
 * (see [ELF1] 1-6).
 */
struct Elf32_Phdr {
	Elf32_Word p_type;
	Elf32_Off p_offset;
	Elf32_Addr p_vaddr;
	Elf32_Addr p_paddr;
	Elf32_Word p_filesz;
	Elf32_Word p_memsz;
	Elf32_Word p_flags;
	Elf32_Word p_align;
};

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL 0 /* Ignore. */
#define PT_LOAD 1 /* Loadable segment. */
#define PT_DYNAMIC 2 /* Dynamic linking info. */
#define PT_INTERP 3 /* Name of dynamic loader. */
#define PT_NOTE 4 /* Auxiliary info. */
#define PT_SHLIB 5 /* Reserved. */
#define PT_PHDR 6 /* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

static bool validate_segment(const struct Elf32_Phdr *, struct file *);
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage,
												 uint32_t read_bytes, uint32_t zero_bytes,
												 bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *EIP
 * and its initial stack pointer into *ESP.
 * Returns true if successful, false otherwise.
 * Prevents denies writes to the file.
 */
bool load(void (**eip)(void), char *file_name)
{
	struct thread *t = thread_current();
	struct Elf32_Ehdr ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	int i;

	/* Allocate and activate page directory. */
	t->pagedir = pagedir_create();
	if (!t->pagedir)
		goto done;
	process_activate();

	/* Open executable file. */
	filesys_enter();
	file = filesys_open(file_name);
	filesys_exit();
	if (!file) {
		printf("%s: %s: open failed\n", __func__, file_name);
		goto done;
	}
	filesys_enter();
	file_deny_write(file); /* Prevents the file from being changed during load */
	filesys_exit();

	/* Read and verify executable header. */
	filesys_enter();
	if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr ||
			memcmp(ehdr.e_ident, "\177ELF\1\1\1", 7) || ehdr.e_type != 2 ||
			ehdr.e_machine != 3 || ehdr.e_version != 1 ||
			ehdr.e_phentsize != sizeof(struct Elf32_Phdr) || ehdr.e_phnum > 1024) {
		filesys_exit();
		printf("%s: %s: error loading executable\n", __func__, file_name);
		goto done;
	}
	filesys_exit();

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Elf32_Phdr phdr;

		filesys_enter();
		if (file_ofs < 0 || file_ofs > file_length(file)) {
			filesys_exit();
			goto done;
		}
		filesys_exit();
		filesys_enter();
		file_seek(file, file_ofs);
		filesys_exit();

		filesys_enter();
		if (file_read(file, &phdr, sizeof phdr) != sizeof phdr) {
			filesys_exit();
			goto done;
		}
		filesys_exit();

		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
		case PT_NULL:
		case PT_NOTE:
		case PT_PHDR:
		case PT_STACK:
		default:
			/* Ignore this segment. */
			break;
		case PT_DYNAMIC:
		case PT_INTERP:
		case PT_SHLIB:
			goto done;
		case PT_LOAD:
			if (validate_segment(&phdr, file)) {
				bool writable = (phdr.p_flags & PF_W) != 0;
				uint32_t file_page = phdr.p_offset & ~PGMASK;
				uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
				uint32_t page_offset = phdr.p_vaddr & PGMASK;
				uint32_t read_bytes, zero_bytes;
				if (phdr.p_filesz > 0) {
					/* Normal segment.
					 * Read initial part from disk and zero the rest.
					 */
					read_bytes = page_offset + phdr.p_filesz;
					zero_bytes =
									(ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) - read_bytes);
				} else {
					/* Entirely zero.
					 * Don't read anything from disk.
					 */
					read_bytes = 0;
					zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
				}
				if (!load_segment(file, file_page, (void *)mem_page, read_bytes,
													zero_bytes, writable))
					goto done;
			} else {
				goto done;
			}
			break;
		}
	}

	/* Start address. */
	*eip = (void (*)(void))ehdr.e_entry;

	t->exec_file = file;
	return true;
done:
	filesys_enter();
	file_close(file);
	filesys_exit();
	return false;
}

/* load() helpers. */

static bool install_page(void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise.
 */
static bool validate_segment(const struct Elf32_Phdr *phdr, struct file *file)
{
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	filesys_enter();
	if (phdr->p_offset > (Elf32_Off)file_length(file)) {
		filesys_exit();
		return false;
	}
	filesys_exit();

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	 * user address space range.
	 */
	if (!is_user_vaddr((void *)phdr->p_vaddr))
		return false;
	if (!is_user_vaddr((void *)(phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	 * address space.
	 */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	 * Not only is it a bad idea to map page 0, but if we allowed
	 * it then user code that passed a null pointer to system calls
	 * could quite likely panic the kernel by way of null pointer
	 * assertions in memcpy(), etc.
	 */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs.
 */
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage,
												 uint32_t read_bytes, uint32_t zero_bytes,
												 bool writable)
{
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(upage) == 0);
	ASSERT(ofs % PGSIZE == 0);

	filesys_enter();
	file_seek(file, ofs);
	filesys_exit();

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes.
		 */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Check if virtual page already allocated */
		struct thread *t = thread_current();
		uint8_t *kpage = pagedir_get_page(t->pagedir, upage);

		if (!kpage) {
			/* Get a new page of memory. */
			kpage = palloc_get_page(PAL_USER);
			if (!kpage)
				return false;

			/* Add the page to the process's address space. */
			if (!install_page(upage, kpage, writable))
				return false;
		}

		/* Load data into the page. */
		filesys_enter();
		if (file_read(file, kpage, page_read_bytes) != (int)page_read_bytes) {
			filesys_exit();
			return false;
		}
		filesys_exit();
		memset(kpage + page_read_bytes, 0, page_zero_bytes);

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
 * user virtual memory.
 */
static bool setup_stack(void **esp, uint16_t stack_size, uint8_t *kpage)
{
	bool success = false;
	if (kpage) {
		success = install_page(((uint8_t *)PHYS_BASE) - PGSIZE, kpage, true);
		if (success)
			*esp = PHYS_BASE - stack_size;
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails.
 */
static bool install_page(void *upage, void *kpage, bool writable)
{
	struct thread *t = thread_current();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there.
	 */
	return (!pagedir_get_page(t->pagedir, upage) &&
					pagedir_set_page(t->pagedir, upage, kpage, writable));
}

/* Atomic operation. Sets the FLAG and returns its value from before when
 * it was set
 */
bool test_and_set(bool *flag)
{
	return __sync_lock_test_and_set(flag, true);
}

/* Aligns the pointer downards with the words */
uintptr_t word_align(uintptr_t ptr)
{
	return ptr - (ptr % sizeof(void *));
}
