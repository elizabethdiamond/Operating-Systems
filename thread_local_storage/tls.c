#include "tls.h"
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_THREADS 128
/*
 * This is a good place to define any data structures you will use in this file.
 * For example:
 *  - struct TLS: may indicate information about a thread's local storage
 *    (which thread, how much storage, where is the storage in memory)
 *  - struct page: May indicate a shareable unit of memory (we specified in
 *    homework prompt that you don't need to offer fine-grain cloning and CoW,
 *    and that page granularity is sufficient). Relevant information for sharing
 *    could be: where is the shared page's data, and how many threads are sharing it
 *  - Some kind of data structure to help find a TLS, searching by thread ID.
 *    E.g., a list of thread IDs and their related TLS structs, or a hash table.
 */
typedef struct thread_local_storage
{
	pthread_t tid;
	unsigned int size; /* size in bytes */
	unsigned int page_num; /* number of pages */
	struct page **pages; /* array of pointers to pages */
} TLS;

struct page {
	unsigned long address; /*start address of page*/
 	int ref_count; /* counter for shared pages*/
};

struct tid_tls_pair
{
	pthread_t tid;
	TLS *tls;
};

/*
 * Now that data structures are defined, here's a good place to declare any
 * global variables.
 */
static struct tid_tls_pair* tid_tls_pairs[MAX_THREADS];
int page_size;
int init;
pthread_mutex_t lock;

/*
 * With global data declared, this is a good point to start defining your
 * static helper functions.
 */

void tls_handle_page_fault(int sig, siginfo_t *si, void *context) 
{
	unsigned long p_fault = ((unsigned long)si->si_addr) & ~(page_size - 1);
	unsigned long page_address;

	// check whether it is a real segfault or because a thread has touched forbidden memory
	// make a brute force scan through all allocated TLS regions
	for (int i = 0; i <= MAX_THREADS; i++) { 
		for (int j = 0; j < tid_tls_pairs[j]->tls->page_num; j++) {
			page_address = tid_tls_pairs[j]->tls->pages[j]->address;
			if (page_address == p_fault) {
				pthread_exit(NULL);
			}
		}
	}
	// if not page fault, set up a normal seg fault
	signal(SIGSEGV, SIG_DFL);
	signal(SIGBUS, SIG_DFL);
	raise(sig);
}

/* initialize on first create */
void tls_init() 
{
	struct sigaction sigact;
 	page_size = getpagesize();
	sigemptyset(&sigact.sa_mask);
 	sigact.sa_flags = SA_SIGINFO;
	//SA_SIGINFO to see if page fault or normal segfault
	sigact.sa_sigaction = tls_handle_page_fault;
	sigaction(SIGBUS, &sigact, NULL);
	sigaction(SIGSEGV, &sigact, NULL);
	init = 1;
}

/* reprotect memory */
void tls_protect(struct page *p) 
{
	if (mprotect((void *)p->address, page_size, 0)) {
		fprintf(stderr, "tls_protect: could not protect page\n");
		exit(1);
 	}
}

/* unprotect memory */
void tls_unprotect(struct page *p) 
{
	if (mprotect((void *)p->address, page_size, PROT_READ | PROT_WRITE)) {
		fprintf(stderr, "tls_unprotect: could not unprotect page\n");
		exit(1);
	}
}

/*
 * Lastly, here is a good place to add your externally-callable functions.
 */ 

int tls_create(unsigned int size)
{	
	pthread_t tid = pthread_self();
	if (pthread_mutex_init(&lock, NULL) != 0) {
    	return 1;
	}

	if (!init) {
		tls_init();
	}
	// check if thread already has local storage
	for (int i = 0; i < MAX_THREADS; i++) {
		if (tid_tls_pairs[i] != 0) {
			if (tid_tls_pairs[i]->tid == tid) {
				return -1;
			}
		}
	}
	// check size > 0
	if (size <= 0) {
		return -1;
	}

	TLS* tls = (TLS*)calloc(1, sizeof(TLS));
	tls->tid = tid;
	tls->size = size;
	tls->page_num = (size-1)/page_size + 1;
	tls->pages = (struct page**)calloc(tls->page_num, sizeof(struct page));

	// allocate all pages for this TLS
	for (unsigned int i = 0; i < tls->page_num; i++) {
		struct page *p;
		p = (struct page *)calloc(1, sizeof(struct page));
		p->address = (unsigned long)mmap(0, page_size, 0, MAP_ANON | MAP_PRIVATE, 0, 0);
		p->ref_count = 1;
		tls->pages[i] = p;
	}

	// add the (thread id→TLS) mapping to global list
	struct tid_tls_pair* new = (struct tid_tls_pair*)malloc(sizeof(struct tid_tls_pair));
	new->tid = tls->tid;
	new->tls = tls;
	for (int i = 0; i < MAX_THREADS; i++) {
		if (tid_tls_pairs[i] == 0) {
			tid_tls_pairs[i] = new;
			break;
		}
	}

	return 0;
}

int tls_destroy()
{	
	pthread_t tid = pthread_self();

	// check if current thread does not have LSA
	TLS* curr = (TLS*)calloc(1, sizeof(TLS));
	int check = 0;
	for (int i = 0; i < MAX_THREADS; i++) {
		if (tid_tls_pairs[i] != 0) {
			if (tid_tls_pairs[i]->tid == tid) {
				curr = tid_tls_pairs[i]->tls;
				check = 1;
			}
		}
	}
	if (check == 0) {
		return -1;
	}

	// clean up all pages
	for (int i = 0; i < curr->page_num; i++) {
		// if the page is not shared by other threads
		if (curr->pages[i]->ref_count == 1) 
		{
			 munmap((void *)curr->pages[i]->address, page_size);
			 free(curr->pages[i]);
		}
		else { // if the page is shared by other threads
			// decrement the reference counter by one
			curr->pages[i]->ref_count--;
		}
	}

	// clean up tls
	free(curr->pages);
	free(curr);

	// remove from the tid_tls_pairs array
	for (int i = 0; i < MAX_THREADS; i++) {
		if (tid_tls_pairs[i] != 0) {
			if (tid_tls_pairs[i]->tid == tid) {
				tid_tls_pairs[i] = 0;
			}
		}
	}

	return 0;
}

int tls_read(unsigned int offset, unsigned int length, char *buffer)
{	
	// check whether current thread does not have local storage
	TLS* curr = (TLS*)calloc(1, sizeof(TLS));
	int check = 0;
	for (int i = 0; i < MAX_THREADS; i++) {
		if (tid_tls_pairs[i] != 0) {
			if (tid_tls_pairs[i]->tid == pthread_self()) {
				curr = tid_tls_pairs[i]->tls;
				check = 1;
			}
		}
	}
	if (check == 0) {
		return -1;
	}
	// check if offset+length > size
	if (offset+length > curr->size) {
		return -1;
	}

	pthread_mutex_lock(&lock);
	// unprotect all pages belong to thread's TLS
	for (int i = 0; i < curr->page_num; i++) {
		tls_unprotect(curr->pages[i]);
	}

	// perform the read operation
	for (int i = 0, idx = offset; idx < (offset + length); i++, idx++) {
		struct page* p;
		unsigned int page_num, page_offset;
		page_num = idx / page_size;
		page_offset = idx % page_size;
		p = curr->pages[page_num];
		char* src = ((char*)p->address) + page_offset;
		buffer[i]= *src;
	}

	// reprotect all pages belong to thread's TLS
	for (int i = 0; i < curr->page_num; i++) {
		tls_protect(curr->pages[i]);
	}
	
	pthread_mutex_unlock(&lock);
	return 0;
}

int tls_write(unsigned int offset, unsigned int length, const char *buffer)
{
	// check whether current thread has local storage
	TLS* curr = (TLS*)calloc(1, sizeof(TLS));
	int check = 0;
	for (int i = 0; i < MAX_THREADS; i++) {
		if (tid_tls_pairs[i] != 0) {
			if (tid_tls_pairs[i]->tid == pthread_self()) {
				curr = tid_tls_pairs[i]->tls;
				check = 1;
			}
		}
	}
	if (check == 0) {
		return -1;
	}
	// check if offset+length > size
	if (offset+length > curr->size) {
		return -1;
	}

	pthread_mutex_lock(&lock);
	// unprotect all pages belong to thread's TLS
	for (int i = 0; i < curr->page_num; i++) {
		tls_unprotect(curr->pages[i]);
	}

	// perform the write operation
	for (int i = 0, idx = offset; idx < (offset + length); i++, idx++) {
		struct page* p;
		unsigned int page_num, page_offset;
		page_num = idx / page_size;
		page_offset = idx % page_size;
		p = curr->pages[page_num];
		// this page is shared so create a private copy 
		if (p->ref_count > 1) 
		{
			struct page* copy;
			/* copy on write */
			copy = (struct page*)calloc(1, sizeof(struct page));
			copy->address = (unsigned long)mmap(0, page_size, PROT_WRITE, MAP_ANON|MAP_PRIVATE, 0, 0);
			copy->ref_count = 1;

			// copy the entire page contents
			memcpy((void*)copy->address,(void*)p->address, page_size);
			curr->pages[page_num] = copy;

			// update the original page
			p->ref_count--;
			tls_protect(p);
			p = copy;
		}
		// copy a single char from buffer for page address with page offset
		char* dst = ((char*)p->address) + page_offset;
		*dst = buffer[i];
	}

	// reprotect all pages belong to thread's TLS
	for (int i = 0; i < curr->page_num; i++) {
		tls_protect(curr->pages[i]);
	}
	pthread_mutex_unlock(&lock);

	return 0;
}

int tls_clone(pthread_t tid)
{	
	//pthread_mutex_lock(&lock);
	// check whether current thread already has local storage
	for (int i = 0; i < MAX_THREADS; i++) {
		if (tid_tls_pairs[i] != 0) {
			if (tid_tls_pairs[i]->tid == pthread_self()) {
				return -1;
			}
		}
	}

	// check whether target thread already has local storage
	TLS* target = (TLS*)calloc(1, sizeof(TLS));
	int check = 0;
	for (int i = 0; i < MAX_THREADS; i++) {
		if (tid_tls_pairs[i] != 0) {
			if (tid_tls_pairs[i]->tid == tid) {
				target = tid_tls_pairs[i]->tls;
				check = 1;
			}
		}
	}
	if (check == 0) {
		return -1;
	}

	// allocate TLS
	TLS* clone = (TLS*)calloc(1, sizeof(TLS));
	clone->tid = pthread_self();
	clone->size = target->size;
	clone->page_num = target->page_num;
	clone->pages = (struct page**)calloc(target->page_num, sizeof(struct page));

	// copy pages and adjust reference count
	for (int i = 0; i < clone->page_num; i++)
    {
        clone->pages[i] = target->pages[i];
        clone->pages[i]->ref_count++;
    }

	// add the (thread id→TLS) mapping to global list
	struct tid_tls_pair* new = (struct tid_tls_pair*)malloc(sizeof(struct tid_tls_pair));
	new->tid = clone->tid;
	new->tls = clone;
	for (int i = 0; i < MAX_THREADS; i++) {
		if (tid_tls_pairs[i] == 0) {
			tid_tls_pairs[i] = new;
			break;
		}
	}

	//pthread_mutex_unlock(&lock);
	return 0;
}
