#include "ec440threads.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

/* You can support more threads. At least support this many. */
#define MAX_THREADS 128

/* Your stack should be this many bytes in size */
#define THREAD_STACK_SIZE 32767

/* Number of microseconds between scheduling events */
#define SCHEDULER_INTERVAL_USECS (50 * 1000)

/* Size of an address in a thread stack */
#define ADDRESS_SIZE 8

/* Extracted from private libc headers. These are not part of the public
 * interface for jmp_buf.
 */
#define JB_RBX 0
#define JB_RBP 1
#define JB_R12 2
#define JB_R13 3
#define JB_R14 4
#define JB_R15 5
#define JB_RSP 6
#define JB_PC 7

/* thread_status identifies the current state of a thread. You can add, rename,
 * or delete these values. This is only a suggestion. */
enum thread_status
{
	TS_EXITED,
	TS_RUNNING,
	TS_READY
};

/* The thread control block stores information about a thread. You will
 * need one of this per thread.
 */
struct thread_control_block {
	/* TODO: add a thread ID */
	pthread_t id;
	/* TODO: add information about its stack */
	char* stack;
	/* TODO: add information about its registers */
	jmp_buf env;
	/* TODO: add information about the status (e.g., use enum thread_status) */
	enum thread_status status;
	/* Add other information you need to manage this thread */
	struct thread_control_block* next; 
};

/* Declare global variables here */
pthread_t thread_id_count;
struct thread_control_block* curr_thread;
typedef struct {
	struct thread_control_block* head;
	struct thread_control_block* tail;
}Queue;
Queue* thread_queue;

/* insert a TCB to the circular list/queue of available runnable threads */
void queue_insert(struct thread_control_block* new_thread) {
	if(thread_queue->head == NULL) {
		thread_queue->head = new_thread;
		thread_queue->tail = new_thread;
		//new_thread->next = thread_queue->head;
	}
	else {
		thread_queue->tail->next = new_thread;
		thread_queue->tail = new_thread;
		thread_queue->tail->next = thread_queue->head;
	}
}

/* determine which is the next thread to run */
struct thread_control_block* get_next_thread() {
	if(curr_thread->next == NULL) {
		return curr_thread;
	}
	else {
		return curr_thread->next;
	}
}

static void schedule(int signal)
{
	/* TODO: implement your round-robin scheduler
	 * 1. Use setjmp() to update your currently-active thread's jmp_buf
	 *    You DON'T need to manually modify registers here.
	 * 2. Determine which is the next thread that should run
	 * 3. Switch to the next thread (use longjmp on that thread's jmp_buf)
	 */

	// update curr_thread's jmp_buf
	if (!setjmp(curr_thread->env)) {
		if(curr_thread->status == TS_RUNNING) {
			curr_thread->status = TS_READY;
		}
		for (int i = 0; i <= MAX_THREADS; i++) {
			curr_thread = get_next_thread();
			if (curr_thread->status == TS_READY) {
				// activate the next read thread
				curr_thread->status = TS_RUNNING;
				longjmp(curr_thread->env, 1);
				break;
			}
		}
	}
	else { // thread called longjmp and is now running
		curr_thread->status = TS_RUNNING;
	}

}

static void scheduler_init()
{
	/* TODO: do everything that is needed to initialize your scheduler. For example:
	 * - Allocate/initialize global threading data structures
	 * - Create a TCB for the main thread. Note: This is less complicated
	 *   than the TCBs you create for all other threads. In this case, your
	 *   current stack and registers are already exactly what they need to be!
	 *   Just make sure they are correctly referenced in your TCB.
	 * - Set up your timers to call schedule() at a 50 ms interval (SCHEDULER_INTERVAL_USECS)
	*/

	// initialize global variables
	thread_id_count = 0;
	curr_thread = (struct thread_control_block*)malloc(sizeof(struct thread_control_block));
	thread_queue = (Queue*)malloc(sizeof(Queue));
	thread_queue->head = NULL;
	thread_queue->tail = NULL;

	// create tcb for main thread and add to runnnable thread queue
	struct thread_control_block* main_thread = (struct thread_control_block*)malloc(sizeof(struct thread_control_block));
	main_thread->id = thread_id_count;
	main_thread->status = TS_READY;
	queue_insert(main_thread);
	curr_thread = main_thread;

	// set up the timer to call schedule()
	struct sigaction act;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_NODEFER;
	act.sa_handler = schedule;
	sigaction(SIGALRM, &act, NULL);
	if(ualarm(SCHEDULER_INTERVAL_USECS, SCHEDULER_INTERVAL_USECS) < 0) {
		perror("ERROR");
	}
}


int pthread_create(
	pthread_t *thread, const pthread_attr_t *attr,
	void *(*start_routine) (void *), void *arg)
{
	// Create the timer and handler for the scheduler. Create thread 0.
	static bool is_first_call = true;
	if (is_first_call)
	{
		is_first_call = false;
		scheduler_init();
	}

	/* TODO: Return 0 on successful thread creation, non-zero for an error.
	 *       Be sure to set *thread on success.
	 * Hints:
	 * The general purpose is to create a TCB:
	 * - Create a stack.
	 * - Assign the stack pointer in the thread's registers. Important: where
	 *   within the stack should the stack pointer be? It may help to draw
	 *   an empty stack diagram to answer that question.
	 * - Assign the program counter in the thread's registers.
	 * - Wait... HOW can you assign registers of that new stack?
	 *   1. call setjmp() to initialize a jmp_buf with your current thread
	 *   2. modify the internal data in that jmp_buf to create a new thread environment
	 *      env->__jmpbuf[JB_...] = ...
	 *      See the additional note about registers below
	 *   3. Later, when your scheduler runs, it will longjmp using your
	 *      modified thread environment, which will apply all the changes
	 *      you made here.
	 * - Remember to set your new thread as TS_READY, but only  after you
	 *   have initialized everything for the new thread.
	 * - Optionally: run your scheduler immediately (can also wait for the
	 *   next scheduling event).
	 */

	/*
	 * Setting registers for a new thread:
	 * When creating a new thread that will begin in start_routine, we
	 * also need to ensure that `arg` is passed to the start_routine.
	 * We cannot simply store `arg` in a register and set PC=start_routine.
	 * This is because the AMD64 calling convention keeps the first arg in
	 * the EDI register, which is not a register we control in jmp_buf.
	 * We provide a start_thunk function that copies R13 to RDI then jumps
	 * to R12, effectively calling function_at_R12(value_in_R13). So
	 * you can call your start routine with the given argument by setting
	 * your new thread's PC to be ptr_mangle(start_thunk), and properly
	 * assigning R12 and R13.
	 *
	 * Don't forget to assign RSP too! Functions know where to
	 * return after they finish based on the calling convention (AMD64 in
	 * our case). The address to return to after finishing start_routine
	 * should be the first thing you push on your stack.
	*/

	struct thread_control_block* my_thread = (struct thread_control_block*)malloc(sizeof(struct thread_control_block));
	char* sp;
	
	// set the thread id 
	thread_id_count += 1;
	my_thread->id = thread_id_count;

	// create the stack and assign a stack pointer sp
	my_thread->stack = (char*)malloc(THREAD_STACK_SIZE);
	sp = my_thread->stack + THREAD_STACK_SIZE;
	unsigned long int exit_addr = (unsigned long int)pthread_exit;
	memcpy(sp - ADDRESS_SIZE, &exit_addr, ADDRESS_SIZE);
	sp -= ADDRESS_SIZE;

	// assign my_thread registers using setjmp 
	setjmp(my_thread->env);
	my_thread->env[0].__jmpbuf[JB_PC] = ptr_mangle((unsigned long int)start_thunk);
	my_thread->env[0].__jmpbuf[JB_R12] = (unsigned long int)start_routine;
	my_thread->env[0].__jmpbuf[JB_R13] = (unsigned long int)arg;
	my_thread->env[0].__jmpbuf[JB_RSP] = ptr_mangle((unsigned long int)sp);

	// set my_thread as ready and add it to the runnable thread queue
	my_thread->status = TS_READY;
	queue_insert(my_thread);

	// set *thread on success
	*thread = my_thread->id;

	// run schedule immediately so "each thread can run when many threads are launched"
	schedule(0);

	return 0;
}

void pthread_exit(void *value_ptr)
{
	/* TODO: Exit the current thread instead of exiting the entire process.
	 * Hints:
	 * - Release all resources for the current thread. CAREFUL though.
	 *   If you free() the currently-in-use stack then do something like
	 *   call a function or add/remove variables from the stack, bad things
	 *   can happen.
	 * - Update the thread's status to indicate that it has exited
	*/

	curr_thread->status = TS_EXITED;
	if(curr_thread->stack != NULL) {
		free(curr_thread->stack);
	}

	// call schedule if a thread remains
	if (curr_thread->next != NULL) {
		free(curr_thread);
		schedule(0);
	}
	else { // no threads remain so exit the program
		exit(0);
	}

	__builtin_unreachable();
}

pthread_t pthread_self(void)
{
	/* TODO: Return the current thread instead of -1
	 * Hint: this function can be implemented in one line, by returning
	 * a specific variable instead of -1.
	 */
	return curr_thread->id;
}

/* Don't implement main in this file!
 * This is a library of functions, not an executable program. If you
 * want to run the functions in this file, create separate test programs
 * that have their own main functions.
 */
