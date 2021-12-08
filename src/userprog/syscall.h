#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <debug.h>
#include <stdbool.h>

void filesys_enter(void);

void filesys_exit(void);

bool filesys_lock_held(void);

void syscall_init(void);

#endif /* userprog/syscall.h */
