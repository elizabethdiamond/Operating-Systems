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
#include <string.h>
#include <errno.h>

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
	TS_READY,
	TS_BLOCKED
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

/* lock functions to disable/enable scheduler 
prevent scheduler from running when threading library is internally in a critical section 
(users of library will use barriers and mutexes for critical sections that are external to thelibrary)
*/
static void lock()
{
	sigset_t sig;
	sigemptyset(&sig);
	sigaddset(&sig, SIGALRM);
	sigprocmask(SIG_BLOCK, &sig, NULL);
}

static void unlock()
{
	sigset_t sig;
	sigemptyset(&sig);
	sigaddset(&sig, SIGALRM);
	sigprocmask(SIG_UNBLOCK, &sig, NULL);
}

/* insert a TCB to the circular list/queue of available runnable threads */
void queue_insert(Queue* queue, struct thread_control_block* new_thread) {
	lock();
	if(queue->head == NULL) {
		queue->head = new_thread;
		queue->tail = new_thread;
	}
	else {
		queue->tail->next = new_thread;
		queue->tail = new_thread;
		queue->tail->next = queue->head;
	}
	unlock();
}

/* determine which is the next thread to run */
struct thread_control_block* get_next_thread() {
	lock();
	if(curr_thread->next == NULL) {
		return curr_thread;
	}
	else {
		return curr_thread->next;
	}
	unlock();
}

static void schedule(int signal)
{
	/* TODO: implement your round-robin scheduler
	 * 1. Use setjmp() to update your currently-active thread's jmp_buf
	 *    You DON'T need to manually modify registers here.
	 * 2. Determine which is the next thread that should run
	 * 3. Switch to the next thread (use longjmp on that thread's jmp_buf)
	 */

	lock();
	// update curr_thread's jmp_buf
	if (!setjmp(curr_thread->env)) {
		if(curr_thread->status == TS_RUNNING) {
			curr_thread->status = TS_READY;
		}
		for (int i = 0; i <= MAX_THREADS; i++) {
			curr_thread = get_next_thread();
			// make it so a blocked thread is not chosen by the scheduler
			if (curr_thread->status == TS_READY) {
				// activate the next ready thread
				curr_thread->status = TS_RUNNING;
				unlock();
				longjmp(curr_thread->env, 1);
				break;
			}
		}
	}
	else { // thread called longjmp and is now running/return back to env
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

	lock();
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
	queue_insert(thread_queue, main_thread);
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

	unlock();
}


int pthread_create(
	pthread_t *thread, const pthread_attr_t *attr,
	void *(*start_routine) (void *), void *arg)
{
	lock();
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
	queue_insert(thread_queue, my_thread);

	// set *thread on success
	*thread = my_thread->id;

	unlock();
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

	lock();
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

	unlock();
	__builtin_unreachable();
}

pthread_t pthread_self(void)
{
	return curr_thread->id;
}

/* Start Project 3 – Thread Synchronization */
/* mutex functions */
struct mutex_node {
	struct thread_control_block* mthread;
	struct mutex_node* next;
};

struct thread_mutex {
	// status of the mutex lock
	bool locked;
	// list of threads (TCBs) waiting for the mutex
	struct mutex_node* wthreads;
};

/* pthread_mutex_init() initializes a given mutex_t */
int pthread_mutex_init(pthread_mutex_t *restrict mutex, const pthread_mutexattr_t *restrict attr) 
{	
	lock();
	struct thread_mutex* _mutex = (struct thread_mutex*)malloc(sizeof(struct thread_mutex));
	_mutex->locked = false;
	_mutex->wthreads = (struct mutex_node*)malloc(sizeof(struct mutex_node));
	_mutex->wthreads = NULL;

	// copy address of my_mutex to the given pthread_mutex_t mutex memory 
	memcpy(&mutex->__align, &_mutex, sizeof(_mutex));

	unlock();
	return 0;
}

/* destroy the referenced mutex */
int pthread_mutex_destroy(pthread_mutex_t *mutex) 
{	
	lock();
	struct thread_mutex* m;
	unsigned long p;
	if (memcpy(&p, &mutex->__align, sizeof(m)) == NULL) {
		return -1;
	}
	m = (struct thread_mutex*)p;
	free(m);
	unlock();
	return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) 
{	
	lock();
	struct thread_mutex* m;
	unsigned long p;
	if (memcpy(&p, &mutex->__align, sizeof(m)) == NULL) {
		return -1;
	}
	m = (struct thread_mutex*)p;

	if (m->locked == false) {
		// curr_thread aquires the lock and proceeds
		m->locked = true;
	}
	else {
		// the thread blocks until the mutex is available
		curr_thread->status = TS_BLOCKED;
		// add thread to list of waiting threads
		struct mutex_node* new = (struct mutex_node*)malloc(sizeof(struct mutex_node)); 
		new->mthread = curr_thread;
		new->next = NULL;

		if (m->wthreads == NULL) {
			m->wthreads = new;
		}
		else{
			struct mutex_node* temp = m->wthreads;
			while(m->wthreads->next){
				m->wthreads = m->wthreads->next;
			}
			m->wthreads->next = new;
			m->wthreads = temp;
		}
		schedule(0);
	}
	
	if (!m->locked) {
		perror("ERROR");
		return -1;
	}

	unlock();
	return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) 
{	
	lock();
	struct thread_mutex* m;
	unsigned long p;
	if (memcpy(&p, &mutex->__align, sizeof(m)) == NULL) {
		return -1;
	}
	m = (struct thread_mutex*)p;

	if (m->wthreads != NULL) {
		m->wthreads->mthread->status = TS_READY;
		m->wthreads = m->wthreads->next;
	}
	else {
		m->wthreads = NULL;
		m->locked = false;
	}
	
	unlock();
	return 0;
}


/* barrier functions */
struct barrier_node {
	struct thread_control_block* bthread;
	struct barrier_node* next;
};

struct thread_barrier {
	// number of threads needed
	int threads_required;
	// count the number of threads in the barrier
	int threads_in;
	// list of threads in the barrier
	struct barrier_node* bthreads;
};

/* pthread_barrier_init() initializes a given barrier_t */
int pthread_barrier_init(pthread_barrier_t *restrict barrier, const pthread_barrierattr_t *restrict attr, unsigned count)
{	
	lock();
	if (count == 0) {
		return EINVAL;
	}

	struct thread_barrier* _barrier = (struct thread_barrier*)malloc(sizeof(struct thread_barrier)); 
	_barrier->threads_required = count;
	_barrier->threads_in = 0;
	_barrier->bthreads = (struct barrier_node*)malloc(sizeof(struct barrier_node)); 
	_barrier->bthreads = NULL;

	memcpy(&barrier->__align, &_barrier, sizeof(_barrier));
	unlock();
	return 0;
}

/* destory the referenced barrier */
int pthread_barrier_destroy(pthread_barrier_t *barrier) 
{	
	lock();
	struct thread_barrier* b;
	unsigned long p;
	if (memcpy(&p, &barrier->__align, sizeof(b)) == NULL) {
		return -1;
	}
	b = (struct thread_barrier*)p;
	struct barrier_node* tmp;
	//if (b->bthreads != NULL) {
		while (b->bthreads != NULL) {
			tmp = b->bthreads;
			b->bthreads = b->bthreads->next;
			free(tmp);
		}
	//}
	//b->bthreads = NULL;
	b->threads_in = 0;
	b->threads_required = 0;
	free(b);
	unlock();
	return 0;
}

/* wait for all threads to reach the barrier then proceed */
int pthread_barrier_wait(pthread_barrier_t *barrier)
{
	/*
	* The pthread_barrier_wait() function enters the referenced barrier. 
	* The calling thread shall not proceed until the required number of threads 
	* (from count in pthread_barrier_init) have already entered the barrier. 
	* Other threads shall be allowed to proceed while this thread is in a barrier 
	* (unless they are also blocked for other reasons). 
	* Upon exiting a barrier, the order that the threads are awoken is undefined. 
	* Exactly one of the returned threads shall return PTHREAD_BARRIER_SERIAL_THREAD 
	* (it does not matter which one). 
	* The rest of the threads shall return 0.
	*/
	lock();
	struct thread_barrier* b;
	unsigned long p;
	if (memcpy(&p, &barrier->__align, sizeof(b)) == NULL) {
		return -1;
	}
	b = (struct thread_barrier*)p;

	b->threads_in += 1;
	if (b->threads_in > b->threads_required) {
      	return EINVAL;
    }

	if (b->threads_in < b->threads_required) {
		curr_thread->status = TS_BLOCKED;
		// add thread to list of waiting threads
		struct barrier_node* new = (struct barrier_node*)malloc(sizeof(struct barrier_node)); 
		new->bthread = curr_thread;
		new->next = NULL;
		
		if (b->bthreads == NULL) {
			b->bthreads = new;
		}
		else{
			struct barrier_node* temp = b->bthreads;
			while(b->bthreads->next){
				b->bthreads = b->bthreads->next;
			}
			b->bthreads->next = new;
			b->bthreads = temp;
		}
		schedule(0);
		return 0;
	}
	else {	// release the threads in the barrier
		struct barrier_node* temp;
		while (b->bthreads != NULL) {
			b->bthreads->bthread->status = TS_READY;
			b->bthreads = b->bthreads->next;
			//printf("thread = %lx freed \n", b->bthreads->bthread->id);
			//temp = b->bthreads;
			//free(temp);
		}
		b->threads_in = 0;
	}

	unlock();
	return PTHREAD_BARRIER_SERIAL_THREAD;
}

/* Don't implement main in this file!
 * This is a library of functions, not an executable program. If you
 * want to run the functions in this file, create separate test programs
 * that have their own main functions.
 */
