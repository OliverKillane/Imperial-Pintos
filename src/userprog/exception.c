#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"

#ifdef VM
#include <string.h>
#include "threads/pte.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "vm/lazy.h"
#include "vm/mmap.h"
#include "vm/swap.h"
#endif

/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill(struct intr_frame *);
static void page_fault(struct intr_frame *);

/* Registers handlers for interrupts that can be caused by user
 * programs.
 *
 * In a real Unix-like OS, most of these interrupts would be
 * passed along to the user process in the form of signals, as
 * described in [SV-386] 3-24 and 3-25, but we don't implement
 * signals.  Instead, we'll make them simply kill the user
 * process.
 *
 * Page faults are an exception.  Here they are treated the same
 * way as other exceptions, but this will need to change to
 * implement virtual memory.
 *
 * Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
 * Reference" for a description of each of these exceptions.
 */
void exception_init(void)
{
	/* These exceptions can be raised explicitly by a user program,
	 * e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
	 * we set DPL==3, meaning that user programs are allowed to
	 * invoke them via these instructions.
	 */
	intr_register_int(3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
	intr_register_int(4, 3, INTR_ON, kill, "#OF Overflow Exception");
	intr_register_int(5, 3, INTR_ON, kill, "#BR BOUND Range Exceeded Exception");

	/* These exceptions have DPL==0, preventing user processes from
	 * invoking them via the INT instruction.  They can still be
	 * caused indirectly, e.g. #DE can be caused by dividing by
	 * 0.
	 */
	intr_register_int(0, 0, INTR_ON, kill, "#DE Divide Error");
	intr_register_int(1, 0, INTR_ON, kill, "#DB Debug Exception");
	intr_register_int(6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
	intr_register_int(7, 0, INTR_ON, kill, "#NM Device Not Available Exception");
	intr_register_int(11, 0, INTR_ON, kill, "#NP Segment Not Present");
	intr_register_int(12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
	intr_register_int(13, 0, INTR_ON, kill, "#GP General Protection Exception");
	intr_register_int(16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
	intr_register_int(19, 0, INTR_ON, kill, "#XF SIMD Floating-Point Exception");

	/* Most exceptions can be handled with interrupts turned on.
	 * We need to disable interrupts for page faults because the
	 * fault address is stored in CR2 and needs to be preserved.
	 */
	intr_register_int(14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Prints exception statistics. */
void exception_print_stats(void)
{
	printf("Exception: %lld page faults\n", page_fault_cnt);
}

/* Handler for an exception (probably) caused by a user process. */
static void kill(struct intr_frame *f)
{
	/* This interrupt is one (probably) caused by a user process.
	 * For example, the process might have tried to access unmapped
	 * virtual memory (a page fault).  For now, we simply kill the
	 * user process.  Later, we'll want to handle page faults in
	 * the kernel.  Real Unix-like operating systems pass most
	 * exceptions back to the process via signals, but we don't
	 * implement them.
	 */

	/* The interrupt frame's code segment value tells us where the
	 * exception originated.
	 */
	switch (f->cs) {
	case SEL_UCSEG:
		/* User's code segment, so it's a user exception, as we
		 * expected.  Kill the user process.
		 */
		printf("%s: dying due to interrupt %#04x (%s).\n", thread_name(), f->vec_no,
					 intr_name(f->vec_no));
		intr_dump_frame(f);
		thread_exit();

	case SEL_KCSEG:
		/* Kernel's code segment, which indicates a kernel bug.
		 * Kernel code shouldn't throw exceptions.  (Page faults
		 * may cause kernel exceptions--but they shouldn't arrive
		 * here.)  Panic the kernel to make the point.
		 */
		intr_dump_frame(f);
		PANIC("Kernel bug - unexpected interrupt in kernel");

	default:
		/* Some other code segment?
		 * Shouldn't happen.  Panic the kernel.
		 */
		printf("Interrupt %#04x (%s) in unknown segment %04x\n", f->vec_no,
					 intr_name(f->vec_no), f->cs);
		PANIC("Kernel bug - this shouldn't be possible!");
	}
}

/* Page fault handler.  This is a skeleton that must be filled in
 * to implement virtual memory.  Some solutions to task 2 may
 * also require modifying this code.
 *
 * At entry, the address that faulted is in CR2 (Control Register
 * 2) and information about the fault, formatted as described in
 * the PF_* macros in exception.h, is in F's error_code member.  The
 * example code here shows how to parse that information.  You
 * can find more information about both of these in the
 * description of "Interrupt 14--Page Fault Exception (#PF)" in
 * [IA32-v3a] section 5.15 "Exception and Interrupt Reference".
 */
static void page_fault(struct intr_frame *f)
{
	bool not_present; /* True: not-present page, false: writing r/o page. */
	bool write; /* True: access was write, false: access was read. */
	bool user; /* True: access by user, false: access by kernel. */
	void *fault_addr; /* Fault address. */

	/* Obtain faulting address, the virtual address that was
	 * accessed to cause the fault.  It may point to code or to
	 * data.  It is not necessarily the address of the instruction
	 * that caused the fault (that's f->eip).
	 * See [IA32-v2a] "MOV--Move to/from Control Registers" and
	 * [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
	 * (#PF)".
	 */
	asm("movl %%cr2, %0" : "=r"(fault_addr));

	/* Get the value of the page table entry of the fault_addr. */
#ifdef VM
	uint32_t pte_val = pagedir_get_raw_pte(thread_current()->pagedir,
																				 pg_round_down(fault_addr));
#endif

	/* Turn interrupts back on (they were only off so that we could
	 * be assured of reading CR2 before it changed).
	 */
	intr_enable();

	/* Count page faults. */
	page_fault_cnt++;

#ifdef VM

	/* Check for page fault on lazy zeroed, lazily loaded and mmaped pages. */
	switch (pte_get_type(pte_val)) {
	/* For mmaped pages:
	 * 1. Get the pointer to the mmap page to load (user_mmap struct).
	 * 2. Load the mmap data from the filesystem to a new frame and set page
	 *    table (done inside MMAP_LOAD).
	 */
	case MMAPED:
		mmap_load(pte_get_user_mmap(pte_val));
		return;

	/* For swapped pages:
	 * 1. Get a new locked frame (potentially by page replacement).
	 * 2. Use the swap id from the page table entry to load from the swap.
	 * 3. Set the page table entry to the frame was loaded to.
	 * 4. Unlock the frame (now can be page replaced as normal).
	 */
	case SWAPPED: {
		void *kpage = frame_get();
		bool writable = swap_load(kpage, pte_get_swapid(pte_val));
		if (!pagedir_set_page(thread_current()->pagedir, pg_round_down(fault_addr),
													kpage, writable))
			NOT_REACHED();
		frame_unlock_swappable(thread_current()->pagedir, pg_round_down(fault_addr),
													 kpage);
		return;
	}

	/* For lazy pages:
	 * 1. Get a new locked frame (potentially by page replacement).
	 * 2. Load the lazy page's contents from the file system to the frame.
	 * 3. Set the page table entry to the frame the data was loaded to.
	 * 4. Unlock the frame (now can be page replaced as normal), and mark as a
	 *    swappable page (evicted to swap space e.g like stack).
	 */
	case LAZY: {
		void *kpage = frame_get();
		lazy_load_lazy(kpage, pte_get_lazy_load(pte_val));
		if (!pagedir_set_page(thread_current()->pagedir, pg_round_down(fault_addr),
													kpage, true))
			NOT_REACHED();
		frame_unlock_swappable(thread_current()->pagedir, pg_round_down(fault_addr),
													 kpage);
		return;
	}

	/* For zeroed pages:
	 * 1. Check: access is valid:
	 *		a. If in stack -> must be above (stack pointer - 32 bytes).
	 *		b. Else it is from the running process's data section.
	 * 2. Get a new locked frame (potentially by page replacement).
	 * 3. Set the frame to all-zeros.
	 * 4. Set the page table entry to the now zeroed frame.
	 * 5. Unlock the frame (now can be page replaced as normal), and mark as a
	 *    swappable page (evicted to swap space e.g like stack).
	 */
	case ZEROED: {
		if (fault_addr >= (f->esp - 32) || fault_addr < STACK_BOTTOM) {
			void *kpage = frame_get();
			memset(kpage, 0, PGSIZE);
			if (!pagedir_set_page(thread_current()->pagedir,
														pg_round_down(fault_addr), kpage,
														pte_is_zeroed_writeable(pte_val)))
				NOT_REACHED();
			frame_unlock_swappable(thread_current()->pagedir,
														 pg_round_down(fault_addr), kpage);
			return;
		}
	}

	/* All other accesses treated as normal (non-VM) page faults. */
	default:
		break;
	}
#endif

	/* Determine cause of page fault. */
	not_present = (f->error_code & PF_P) == 0;
	write = (f->error_code & PF_W) != 0;
	user = (f->error_code & PF_U) != 0;

	/* Used to check addresses are valid in PUT_USER and GET_USER. */
	if (!user
#ifndef NDEBUG
			&& thread_current()->may_page_fault
#endif
	) {
		f->eip = (void *)f->eax;
		f->eax = -1;
		return;
	}

	printf("Page fault at %p: %s error %s page in %s context.\n", fault_addr,
				 not_present ? "not present" : "rights violation",
				 write ? "writing" : "reading", user ? "user" : "kernel");
	kill(f);
}
