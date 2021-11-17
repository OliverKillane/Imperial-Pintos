#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <debug.h>

/* Global lock for accessing files */
struct lock filesys_lock;

void filesys_enter(void);

void filesys_exit(void);

void syscall_init(void);

#endif /* userprog/syscall.h */
