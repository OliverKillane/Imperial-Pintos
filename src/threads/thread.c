#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/priority.h"
#include "devices/timer.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
 * Used to detect stack overflow.  See the big comment at the top
 * of thread.h for details.
 */
#define THREAD_MAGIC 0xcd6abf4b

/* List of all processes.  Processes are added to this list
 * when they are first scheduled and removed when they exit.
 */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame {
	void *eip; /* Return address. */
	thread_func *function; /* Function to call. */
	void *aux; /* Auxiliary data for function. */
};

/* Statistics. */
static long long idle_ticks; /* # of timer ticks spent idle. */
static long long kernel_ticks; /* # of timer ticks in kernel threads. */
static long long user_ticks; /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4 /* # of timer ticks to give each thread. */
static unsigned thread_ticks; /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
 * If true, use multi-level feedback queue scheduler.
 * Controlled by kernel command-line option "-mlfqs".
 */
bool thread_mlfqs;

/* Number of current ready threads (not running) */
static int32_t num_ready_threads;

/* List of all non-empty ready_queues, in ascending order by priority.*/
static struct list nonempty_ready_queues;

/* Array of all ready lists, each ready list at priority (index + PRI_MIN). */
static struct ready_queue ready_queue_array[1 + PRI_MAX - PRI_MIN];

/* The load-average of the CPU. */
static fixed32 load_average;

static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);
static struct thread *running_thread(void);
static struct thread *next_thread_to_run(void);
static void init_thread_donation(struct thread *, const char *name,
																 int8_t base_priority);
static void init_thread_mlfqs(struct thread *, const char *name, int8_t nice,
															fixed32 recent_cpu);
static void init_thread(struct thread *, const char *name);
static bool is_thread(struct thread *) UNUSED;
static void *alloc_frame(struct thread *, size_t size);
static void schedule(void);
void thread_schedule_tail(struct thread *prev);
static tid_t allocate_tid(void);
static void priority_yield(void);
static int clamp(int value, int max, int min);
static void ready_push(struct thread *thread);
static void ready_remove(struct thread *thread);
static struct thread *ready_pop(void);
static struct thread *ready_front(void);
static void mlfqs_update_priority(struct thread *thread);
static bool ready_queue_cmp(const struct list_elem *a,
														const struct list_elem *b, void *aux UNUSED);
static void mlfqs_load_avg_decay(void);
static void mlfqs_decay_thread(struct thread *thread, void *aux UNUSED);

/* Initializes the threading system by transforming the code
 * that's currently running into a thread.  This can't work in
 * general and it is possible in this case only because loader.S
 * was careful to put the bottom of the stack at a page boundary.
 *
 * Also initializes the run queue and the tid lock.
 *
 * After calling this function, be sure to initialize the page
 * allocator before trying to create any threads with
 * thread_create().
 *
 * It is not safe to call thread_current() until this function
 * finishes.
 */
void thread_init(void)
{
	ASSERT(intr_get_level() == INTR_OFF);

	lock_init(&tid_lock);
	list_init(&all_list);

	/* Initialise the priority scheduling system with 0 ready threads. */
	num_ready_threads = 0;

	/* Initialise round robin queue for each priority. */
	for (int index = 0; index <= PRI_MAX - PRI_MIN; index++)
		list_init(&ready_queue_array[index].thread_queue);

	/* Initialise the list of nonempty thread queues. */
	list_init(&nonempty_ready_queues);

	/* Set up a thread structure for the running thread and
	 * start load_avg at zero if mlfqs used.
	 */
	initial_thread = running_thread();
	if (thread_mlfqs) {
		load_average = int_to_fixed(0);
		init_thread_mlfqs(initial_thread, "main", NICE_DEFAULT, int_to_fixed(0));
	} else {
		init_thread(initial_thread, "main");
	}

	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid();
}

/* Starts preemptive thread scheduling by enabling interrupts.
 * Also creates the idle thread.
 */
void thread_start(void)
{
	/* We have to delay the initialization of the donation in the initial thread
	 * because it requires malloc to work.
	 */
	if (!thread_mlfqs)
		donation_thread_init(initial_thread, PRI_DEFAULT);

	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init(&idle_started, 0);
	thread_create("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	intr_enable();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down(&idle_started);
}

/* Returns the number of threads currently in the ready list */
size_t threads_ready(void)
{
	return num_ready_threads;
}

static struct thread *ready_front(void)
{
	enum intr_level old_level;
	struct thread *front_thread;
	old_level = intr_disable();

	/* If there are no ready threads, return null. */
	if (num_ready_threads == 0)
		front_thread = NULL;
	else
		front_thread = list_entry(
						list_front(&list_entry(list_front(&nonempty_ready_queues),
																	 struct ready_queue, elem)
																->thread_queue),
						struct thread, elem);

	intr_set_level(old_level);
	return front_thread;
}

/* Pops the highest priority thread from the ready threads. */
static struct thread *ready_pop(void)
{
	enum intr_level old_level;
	struct thread *next_thread;
	old_level = intr_disable();

	if (num_ready_threads == 0) {
		next_thread = NULL;
	} else {
		struct ready_queue *rq = list_entry(list_front(&nonempty_ready_queues),
																				struct ready_queue, elem);
		next_thread =
						list_entry(list_pop_front(&rq->thread_queue), struct thread, elem);

		if (list_empty(&rq->thread_queue))
			list_remove(&rq->elem);

		num_ready_threads--;
	}
	intr_set_level(old_level);
	return next_thread;
}

/* update a thread's position in the priority queue. */
void ready_queue_update(struct thread *thread)
{
	/* interrupts disabled, an interrupt may modify the priority of the thread */
	enum intr_level old_level;
	old_level = intr_disable();
	ready_remove(thread);
	ready_push(thread);
	intr_set_level(old_level);
}

/* If A is lower priority than B, return true. Used for inserting ready_queues
 * into nonempty_ready_queues, placing highest priority ready_queues at the
 * front.
 */
static bool ready_queue_cmp(const struct list_elem *a,
														const struct list_elem *b, void *aux UNUSED)
{
	/* Higher priorities will be later in the nonempty_ready_queues array,
	 * so I can just compare pointers
	 */
	return a > b;
}

static void ready_push(struct thread *thread)
{
	ASSERT(thread->priority >= PRI_MIN && thread->priority <= 63);

	enum intr_level old_level;
	old_level = intr_disable();

	struct ready_queue *r_list = &ready_queue_array[thread->priority - PRI_MIN];

	if (list_empty(&r_list->thread_queue))
		list_insert_ordered(&nonempty_ready_queues, &r_list->elem, ready_queue_cmp,
												NULL);

	/* Add thread to the correct ready_queue */
	list_push_back(&r_list->thread_queue, &thread->elem);
	num_ready_threads++;
	intr_set_level(old_level);
}

/* Makes the scheduler acknowledge that the thread has been blocked from
 * the queue.
 *
 * The thread must already be in the tqueue data structure.
 */
static void ready_remove(struct thread *thread)
{
	enum intr_level old_level;
	old_level = intr_disable();

	/* if the last element in the list, then get the list it is part of, then
	 * the ready_queue that it is part of, and remove that ready_queue from the
	 * nonempty_ready_queues
	 */

	if (list_elem_alone(&thread->elem))
		list_remove(&((struct ready_queue *)get_list(&thread->elem))->elem);

	list_remove(&thread->elem);
	num_ready_threads--;

	intr_set_level(old_level);
}

/* Called by the timer interrupt handler at each timer tick.
 * Thus, this function runs in an external interrupt context.
 */
void thread_tick(void)
{
	struct thread *cur = thread_current();
	if (thread_mlfqs) {
		/* increment the recent cpu */
		if (cur != idle_thread)
			cur->recent_cpu = add_f_i(cur->recent_cpu, 1);

		if (timer_ticks() % TIMER_FREQ == 0) {
			mlfqs_load_avg_decay();
			thread_foreach(mlfqs_decay_thread, NULL);
		}
	}

	/* Update statistics. */
	if (cur == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (cur->pagedir)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE) {
		if (thread_mlfqs && cur != idle_thread)
			mlfqs_update_priority(cur);

		intr_yield_on_return();
	}
}

/* Prints thread statistics. */
void thread_print_stats(void)
{
	printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
				 idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
 * PRIORITY, which executes FUNCTION passing AUX as the argument,
 * and adds it to the ready queue.  Returns the thread identifier
 * for the new thread, or TID_ERROR if creation fails.
 *
 * If thread_start() has been called, then the new thread may be
 * scheduled before thread_create() returns.  It could even exit
 * before thread_create() returns.  Contrariwise, the original
 * thread may run for any amount of time before the new thread is
 * scheduled.  Use a semaphore or some other form of
 * synchronization if you need to ensure ordering.
 *
 * The code provided sets the new thread's `priority' member to
 * PRIORITY, but no actual priority scheduling is implemented.
 * Priority scheduling is the goal of Problem 1-3.
 */
tid_t thread_create(const char *name, int priority, thread_func *function,
										void *aux)
{
	struct thread *t;
	struct kernel_thread_frame *kf;
	struct switch_entry_frame *ef;
	struct switch_threads_frame *sf;
	tid_t tid;
	enum intr_level old_level;

	ASSERT(function);

	/* Allocate thread. */
	t = palloc_get_page(PAL_ZERO);
	if (!t)
		return TID_ERROR;

	/* Initialize thread, set priority of new thread.
	 * If mlfqs and the idle thread is set, inherit
	 * the parent's niceness, recent_cpu and therefore priority.
	 */
	struct thread *cur = thread_current();
	if (thread_mlfqs && idle_thread)
		init_thread_mlfqs(t, name, cur->nice, cur->recent_cpu);
	else
		init_thread_donation(t, name, priority);

	tid = t->tid = allocate_tid();

	/* Prepare thread for first run by initializing its stack.
	 * Do this atomically so intermediate values for the 'stack'
	 * member cannot be observed.
	 */
	old_level = intr_disable();

	/* Stack frame for kernel_thread(). */
	kf = alloc_frame(t, sizeof *kf);
	kf->eip = NULL;
	kf->function = function;
	kf->aux = aux;

	/* Stack frame for switch_entry(). */
	ef = alloc_frame(t, sizeof *ef);
	ef->eip = (void (*)(void))kernel_thread;

	/* Stack frame for switch_threads(). */
	sf = alloc_frame(t, sizeof *sf);
	sf->eip = switch_entry;
	sf->ebp = 0;

	intr_set_level(old_level);

	/* Add to run queue. */
	thread_unblock(t);

	return tid;
}

/* If the current thread's priority is no longer the highest, yield. */
static void priority_yield(void)
{
	if (num_ready_threads > 0 &&
			ready_front()->priority > thread_current()->priority) {
		if (intr_context())
			intr_yield_on_return();
		else
			thread_yield();
	}
}

/* Puts the current thread to sleep.  It will not be scheduled
 * again until awoken by thread_unblock().
 *
 * This function must be called with interrupts turned off.  It
 * is usually a better idea to use one of the synchronization
 * primitives in synch.h.
 */

void thread_block(void)
{
	ASSERT(!intr_context());
	ASSERT(intr_get_level() == INTR_OFF);

	thread_current()->status = THREAD_BLOCKED;
	schedule();
}

/* Transitions a blocked thread T to the ready-to-run state.
 * This is an error if T is not blocked.  (Use thread_yield() to
 * make the running thread ready.)
 *
 * This function does not preempt the running thread.  This can
 * be important: if the caller had disabled interrupts itself,
 * it may expect that it can atomically unblock a thread and
 * update other data.
 */
void thread_unblock(struct thread *t)
{
	enum intr_level old_level;

	ASSERT(is_thread(t));

	old_level = intr_disable();
	ASSERT(t->status == THREAD_BLOCKED);

	ready_push(t);
	t->status = THREAD_READY;
	priority_yield();

	intr_set_level(old_level);
}

/* Returns the name of the running thread. */
const char *thread_name(void)
{
	return thread_current()->name;
}

/* Returns the running thread.
 * This is running_thread() plus a couple of sanity checks.
 * See the big comment at the top of thread.h for details.
 */
struct thread *thread_current(void)
{
	struct thread *t = running_thread();

	/* Make sure T is really a thread.
	 * If either of these assertions fire, then your thread may
	 * have overflowed its stack.  Each thread has less than 4 kB
	 * of stack, so a few big automatic arrays or moderate
	 * recursion can cause stack overflow.
	 */
	ASSERT(is_thread(t));
	ASSERT(t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t thread_tid(void)
{
	return thread_current()->tid;
}

/* Deschedules the current thread and destroys it.  Never
 * returns to the caller.
 */
void thread_exit(void)
{
	ASSERT(!intr_context());

#ifdef USERPROG
	process_exit();
#endif

	/* Remove thread from all threads list, set our status to dying,
	 * and schedule another process.  That process will destroy us
	 * when it calls thread_schedule_tail().
	 */
	intr_disable();
	list_remove(&thread_current()->allelem);
	thread_current()->status = THREAD_DYING;
	schedule();
	NOT_REACHED();
}

/* Yields the CPU.  The current thread is not put to sleep and
 * may be scheduled again immediately at the scheduler's whim.
 */
void thread_yield(void)
{
	struct thread *cur = thread_current();
	enum intr_level old_level;

	ASSERT(!intr_context());

	old_level = intr_disable();
	if (cur != idle_thread)
		ready_push(cur);

	cur->status = THREAD_READY;
	schedule();
	intr_set_level(old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
 * This function must be called with interrupts off.
 */
void thread_foreach(thread_action_func *func, void *aux)
{
	ASSERT(intr_get_level() == INTR_OFF);
	for (struct list_elem *e = list_begin(&all_list); e != list_end(&all_list);
			 e = list_next(e)) {
		struct thread *t = list_entry(e, struct thread, allelem);
		func(t, aux);
	}
}

/* If mlfqs is off, sets the current thread's priority to new priority.
 * If the current thread no longer has the highest priority, yield.
 */
void thread_set_priority(int new_priority)
{
	enum intr_level old_level;

	old_level = intr_disable();
	if (!thread_mlfqs) {
		struct thread *cur = thread_current();
		int old_priority = cur->priority;

		cur->priority = new_priority;
		if (old_priority > new_priority)
			priority_yield();
	}
	intr_set_level(old_level);
}

/* Returns the current thread's priority. */
int thread_get_priority(void)
{
	return thread_current()->priority;
}

/* Sets the current thread's nice value to nice, and updates priority
 * accordingly. Called when mlfqs is on.
 * If the current thread no longer has the highest priority, yield.
 */
void thread_set_nice(int nice)
{
	enum intr_level old_level;
	struct thread *cur = thread_current();

	old_level = intr_disable();
	cur->nice = clamp(nice, NICE_MAX, NICE_MIN);
	mlfqs_update_priority(cur);
	priority_yield();
	intr_set_level(old_level);
}

/* Returns the current thread's nice value. */
int thread_get_nice(void)
{
	return (int)thread_current()->nice;
}

/* Returns 100 times the system load average. */
int thread_get_load_avg(void)
{
	return fixed_to_int_round(mult_f_i(load_average, 100));
}

/* Returns 100 times the current thread's recent_cpu value. */
int thread_get_recent_cpu(void)
{
	return fixed_to_int_round(mult_f_i(thread_current()->recent_cpu, 100));
}

bool priority_cmp(const struct list_elem *a, const struct list_elem *b,
									void *aux UNUSED)
{
	return list_entry(b, struct thread, elem)->priority >
				 list_entry(a, struct thread, elem)->priority;
}

/* Idle thread.  Executes when no other thread is ready to run.
 *
 * The idle thread is initially put on the ready list by
 * thread_start().  It will be scheduled once initially, at which
 * point it initializes idle_thread, "up"s the semaphore passed
 * to it to enable thread_start() to continue, and immediately
 * blocks.  After that, the idle thread never appears in the
 * ready list.  It is returned by next_thread_to_run() as a
 * special case when the ready list is empty.
 */
static void idle(void *idle_started_ UNUSED)
{
	struct semaphore *idle_started = idle_started_;
	idle_thread = thread_current();
	sema_up(idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable();
		thread_block();

		/* Re-enable interrupts and wait for the next one.
		 *
		 * The `sti' instruction disables interrupts until the
		 * completion of the next instruction, so these two
		 * instructions are executed atomically.  This atomicity is
		 * important; otherwise, an interrupt could be handled
		 * between re-enabling interrupts and waiting for the next
		 * one to occur, wasting as much as one clock tick worth of
		 * time.
		 *
		 * See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		 * 7.11.1 "HLT Instruction".
		 */
		asm volatile("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void kernel_thread(thread_func *function, void *aux)
{
	ASSERT(function);

	intr_enable(); /* The scheduler runs with interrupts off. */
	function(aux); /* Execute the thread function. */
	thread_exit(); /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *running_thread(void)
{
	uint32_t *esp;

	/* Copy the CPU's stack pointer into `esp', and then round that
	 * down to the start of a page.  Because `struct thread' is
	 * always at the beginning of a page and the stack pointer is
	 * somewhere in the middle, this locates the curent thread.
	 */
	asm("mov %%esp, %0" : "=g"(esp));
	return pg_round_down(esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool is_thread(struct thread *t)
{
	return t && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named NAME, with priority
 * donation base priority as BASE_PRIORITY.
 */
static void init_thread_donation(struct thread *t, const char *name,
																 int8_t base_priority)
{
	init_thread(t, name);
	donation_thread_init(t, base_priority);
}

/* Does basic initialization of T as a blocked thread named NAME, with mlfqs
 * values for niceness as NICE and recent_cpu as RECENT_CPU.
 */
static void init_thread_mlfqs(struct thread *t, const char *name, int8_t nice,
															fixed32 recent_cpu)
{
	t->recent_cpu = recent_cpu;
	t->nice = nice;
	mlfqs_update_priority(t);
	init_thread(t, name);
}

/* Does basic initialization of T as a blocked thread named
 * NAME.
 */
static void init_thread(struct thread *t, const char *name)
{
	enum intr_level old_level;

	ASSERT(t);
	ASSERT(name);

	memset(t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy(t->name, name, sizeof t->name);
	t->stack = (uint8_t *)t + PGSIZE;
	t->magic = THREAD_MAGIC;

	old_level = intr_disable();
	list_push_back(&all_list, &t->allelem);
	intr_set_level(old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
 * returns a pointer to the frame's base.
 */
static void *alloc_frame(struct thread *t, size_t size)
{
	/* Stack data is always allocated in word-size units. */
	ASSERT(is_thread(t));
	ASSERT(size % sizeof(uint32_t) == 0);

	t->stack -= size;
	return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
 * return a thread from the run queue, unless the run queue is
 * empty.  (If the running thread can continue running, then it
 * will be in the run queue.)  If the run queue is empty, return
 * idle_thread.
 */
static struct thread *next_thread_to_run(void)
{
	if (num_ready_threads == 0)
		return idle_thread;
	else
		return ready_pop();
}

/* Completes a thread switch by activating the new thread's page
 * tables, and, if the previous thread is dying, destroying it.
 *
 * At this function's invocation, we just switched from thread
 * PREV, the new thread is already running, and interrupts are
 * still disabled.  This function is normally invoked by
 * thread_schedule() as its final action before returning, but
 * the first time a thread is scheduled it is called by
 * switch_entry() (see switch.S).
 *
 * It's not safe to call printf() until the thread switch is
 * complete.  In practice that means that printf()s should be
 * added at the end of the function.
 *
 * After this function and its caller returns, the thread switch
 * is complete.
 */
void thread_schedule_tail(struct thread *prev)
{
	struct thread *cur = running_thread();

	ASSERT(intr_get_level() == INTR_OFF);

	/* Mark us as running. */
	cur->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate();
#endif

	/* If the thread we switched from is dying, destroy its struct
	 * thread.  This must happen late so that thread_exit() doesn't
	 * pull out the rug under itself.  (We don't free
	 * initial_thread because its memory was not obtained via
	 * palloc().)
	 */
	if (prev && prev->status == THREAD_DYING && prev != initial_thread) {
		ASSERT(prev != cur);
		palloc_free_page(prev);
	}
}

/* Schedules a new process.  At entry, interrupts must be off and
 * the running process's state must have been changed from
 * running to some other state.  This function finds another
 * thread to run and switches to it.
 *
 * It's not safe to call printf() until thread_schedule_tail()
 * has completed.
 */

static void schedule(void)
{
	struct thread *cur = running_thread();
	struct thread *next = next_thread_to_run();
	struct thread *prev = NULL;

	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(cur->status != THREAD_RUNNING);
	ASSERT(is_thread(next));

	if (cur != next)
		prev = switch_threads(cur, next);
	thread_schedule_tail(prev);
}

/* Returns a tid to use for a new thread. */
static tid_t allocate_tid(void)
{
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire(&tid_lock);
	tid = next_tid++;
	lock_release(&tid_lock);

	return tid;
}

/* Offset of `stack' member within `struct thread'.
 * Used by switch.S, which can't figure it out on its own.
 */
uint32_t thread_stack_ofs = offsetof(struct thread, stack);

/* Recalculates the recent_cpu of a thread according to the formula:
 * recent_cpu = (2 * load_avg) / (2 * load_avg + 1) * recent_cpu + niceness
 */
static void mlfqs_decay_thread(struct thread *thread, void *aux UNUSED)
{
	if (thread != idle_thread) {
		int8_t nice = thread->nice;
		fixed32 recent_cpu = thread->recent_cpu;

		/* Fixed Point calculations for new recent_cpu */
		fixed32 twice_load_avg = mult_f_i(load_average, 2);
		fixed32 twice_load_avg_plus_one = add_f_i(twice_load_avg, 1);
		fixed32 load_avg_ratio = div(twice_load_avg, twice_load_avg_plus_one);
		fixed32 discounted_recent_cpu = mult(load_avg_ratio, recent_cpu);
		fixed32 new_recent_cpu = add_f_i(discounted_recent_cpu, nice);

		thread->recent_cpu = new_recent_cpu;

		/* Update the priority */
		mlfqs_update_priority(thread);

		/* update the position in the thread queues */
		if (thread->status == THREAD_READY)
			ready_queue_update(thread);
	}
}

/* Recalculates the load average according to the formula:
 * load_avg = (59/60) * load_avg + (1/60) * ready/running threads
 *
 * If idling, 0 threads, if not num_ready_threads + 1 (running thread).
 */
static void mlfqs_load_avg_decay(void)
{
	fixed32 fifty_nine_sixty = div_f_i(int_to_fixed(59), 60);
	fixed32 one_sixty = div_f_i(int_to_fixed(1), 60);
	load_average = add(mult(fifty_nine_sixty, load_average),
										 mult_f_i(one_sixty, thread_current() == idle_thread ?
																								 0 :
																								 num_ready_threads + 1));
}

/* Sets a thread's priority based on its niceness and recent_cpu. */
static void mlfqs_update_priority(struct thread *thread)
{
	fixed32 recent_cpu = thread->recent_cpu;
	int8_t nice = thread->nice;
	int new_priority =
					PRI_MAX - fixed_to_int_round(div_f_i(recent_cpu, 4)) - (nice * 2);
	thread->priority = clamp(new_priority, PRI_MAX, PRI_MIN);
}

/* clamp a value between a minimum and maximum. */
static int clamp(int value, int max, int min)
{
	if (value > max)
		return max;
	else if (value < min)
		return min;
	else
		return value;
}
