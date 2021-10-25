#ifndef TESTS_DEVICES_TESTS_H
#define TESTS_DEVICES_TESTS_H

void run_test(const char *arg);

typedef void test_func(void);

extern test_func test_alarm_single;
extern test_func test_alarm_multiple;
extern test_func test_alarm_simultaneous;
extern test_func test_alarm_no_busy_wait;
extern test_func test_alarm_one;
extern test_func test_alarm_zero;
extern test_func test_alarm_negative;
extern test_func test_pqueue;

#ifdef THREADS
extern test_func test_fixed_point;
extern test_func test_alarm_priority;
extern test_func test_priority_change;
extern test_func test_priority_donate_one;
extern test_func test_priority_donate_multiple;
extern test_func test_priority_donate_multiple2;
extern test_func test_priority_donate_sema;
extern test_func test_priority_donate_nest;
extern test_func test_priority_donate_lower;
extern test_func test_priority_donate_chain;
extern test_func test_priority_preservation;
extern test_func test_priority_fifo;
extern test_func test_priority_preempt;
extern test_func test_priority_sema;
extern test_func test_priority_condvar;
extern test_func test_mlfqs_load_1;
extern test_func test_mlfqs_load_60;
extern test_func test_mlfqs_load_avg;
extern test_func test_mlfqs_recent_1;
extern test_func test_mlfqs_fair_2;
extern test_func test_mlfqs_fair_20;
extern test_func test_mlfqs_nice_2;
extern test_func test_mlfqs_nice_10;
extern test_func test_mlfqs_block;
#endif

void msg(const char *arg, ...);
void fail(const char *arg, ...);
void pass(void);

#endif /* tests/devices/tests.h */
