#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"
#include <list.h>

/* The bottom of the lazy-zeroed stack */
#define STACK_MAX_SIZE 0x400000
#define STACK_BOTTOM PHYS_BASE - STACK_MAX_SIZE

/* Process identifier. (duplicated code from src/user/syscall.h) */
typedef int pid_t;
#define PID_ERROR ((pid_t)-1)

/* Child process manager. */
struct child_manager {
	bool release; /* Whether the other side should free it. 0 by default */
	int exit_status; /* The status for the wait command. -1 by default */
	struct semaphore wait_sema; /* For waiting on child to die. 0 by default */
	struct list_elem elem; /* Elem for the list of children in the parent */
	tid_t tid; /* Tid of the child process */
};

tid_t process_execute(const char *file_name);
int process_wait(tid_t child_tid);
void process_exit(void);
void process_activate(void);
void process_set_exit_status(int code);

#endif /* userprog/process.h */
