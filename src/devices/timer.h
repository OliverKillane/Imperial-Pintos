#ifndef DEVICES_TIMER_H
#define DEVICES_TIMER_H

#include <round.h>
#include <stdint.h>
#include "threads/thread.h"
#include "threads/synch.h"
#include <priority_queue.h>

/* Number of timer interrupts per second. */
#define TIMER_FREQ 100

void timer_init(void);
void timer_calibrate(void);

int64_t timer_ticks(void);
int64_t timer_elapsed(int64_t then);

/* Timer structure to handle the timer_sleep calls */
struct sleep_entry {
	struct pqueue_elem elem;
	struct semaphore thread_sema;
	int64_t end;
};

/* Sleep and yield the CPU to other threads. */
void timer_sleep(int64_t ticks);
void timer_msleep(int64_t milliseconds);
void timer_usleep(int64_t microseconds);
void timer_nsleep(int64_t nanoseconds);

/* Busy waits. */
void timer_mdelay(int64_t milliseconds);
void timer_udelay(int64_t microseconds);
void timer_ndelay(int64_t nanoseconds);

void timer_print_stats(void);

#endif /* devices/timer.h */
