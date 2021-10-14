// TODO: add internal functions
// TODO: add implementations
// TODO: add template for the file

#include "threads/priority.h"
#include "threads/thread.h"


/* Informs the scheduler that the priority of the thread has changed */
static void tqueue_priority_update(struct thread *thread); 
static void donation_init();
static void mlfqs_init();

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-mlfqs". */
bool thread_mlfqs;

/* Initializes the priority scheduler and its components */
void
tqueue_init() {
  // TODO: better description (like the ones in synch.c)
  // TODO: implementation
}

/* Retreives the next thread to be scheduled without popping it from the queue */
struct thread *
tqueue_front() {
  // TODO: better description (like the ones in synch.c)
  // TODO: implementation
}

/* Pops the next thread to be scheduled from the queue and returns it */
struct thread *
tqueue_next() {
  // TODO: better description (like the ones in synch.c)
  // TODO: implementation
}

/* Makes the scheduler acknowledge the thread and inherit the priority 
   parameters from the parent thread */
void
tqueue_thread_init(struct thread *thread, struct thread *parent) {
  // TODO: better description (like the ones in synch.c)
  // TODO: implementation
}

/* Makes the scheduler acknowledge that the thread has been unblocked */
void
tqueue_add(struct thread *thread) {
  // TODO: better description (like the ones in synch.c)
  // TODO: implementation
}

/* Makes the scheduler acknowledge that the thread has been blocked from 
   the queue */
void
tqueue_remove(struct thread *thread) {
  // TODO: better description (like the ones in synch.c)
  // TODO: implementation
}

/* Sets the base priority of a thread */
void
donation_set_priority(struct thread *thread, int priority) {
  // TODO: better description (like the ones in synch.c)
  // TODO: implementation
}

/* Gets the base priority of a thread */
int
donation_get_priority(const struct thread *thread) {
  // TODO: better description (like the ones in synch.c)
  // TODO: implementation
}

/* Starts priority donation from one thread to another */  
void
donation_link(struct thread *donator, struct thread *donatee) {
  // TODO: better description (like the ones in synch.c)
  // TODO: implementation
}

/* Stops all priority donations from that specific thread */
void
donation_unlink(struct thread *donator) {
  // TODO: better description (like the ones in synch.c)
  // TODO: implementation
}

/* Sets the base niceness value */
void
mlfqs_set_nice(struct thread *thread, int nice) {
  // TODO: better description (like the ones in synch.c)
  // TODO: implementation
}

/* Gets the base niceness value */
int
mlfqs_get_nice(const struct thread *thread) {
  // TODO: better description (like the ones in synch.c)
  // TODO: implementation
}

/* Increments the recent_cpu of the thread */
void
mlfqs_tick(struct thread *thread) {
  // TODO: better description (like the ones in synch.c)
  // TODO: implementation
}

/* Returns the recent_cpu of the thread */
int
mlfqs_get_recent_cpu(const struct thread *thread) {
  // TODO: better description (like the ones in synch.c)
  // TODO: implementation
}

/* Returns the priority calculated using mlfqs */
void
mlfqs_get_priority(const struct thread *thread) {
  // TODO: better description (like the ones in synch.c)
  // TODO: implementation
}

/* Performs exponential average step on all recent_cpu and load_avg */
void
mlfqs_decay() {
  // TODO: better description (like the ones in synch.c)
  // TODO: implementation
}

/* Returns the load average */
int
mlfqs_get_load_avg() {
  // TODO: better description (like the ones in synch.c)
  // TODO: implementation
}
