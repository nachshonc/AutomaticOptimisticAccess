#define _GNU_SOURCE
#include <pthread.h>
#include "lfl.h" 
#include "lfhash.h"
#include "worker.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include "rand.h"
#include "debugging.h"
__thread unsigned long next; //for rand.h
extern long num_ops;
__thread int __tid;

void init_thread(ThreadGlobals* tg){
#ifdef ALLOW_PINNING
		pin (gettid(), tg->input.threadID);// args->id/8 + 4*(args->id % 8));
#endif
	simSRandom(tg->input.threadID*12345);
	__tid=tg->input.threadID;
}
/*int m;*/
void destroy_thread(ThreadGlobals* tg){
	/*int cntfree();
	int vl=cntfree();
	__sync_fetch_and_add(&m, vl);
	printf("thread %d free %d\n", tg->input.threadID, vl);*/
}

void* start_routine(void *arg) {
    ThreadGlobals* tg	= (ThreadGlobals*) arg;
    long my_ops=0;
    int tmpData; //for the return value of searches.
    init_thread(tg);
	while (run == FALSE) {			// busy-wait to start "simultaneously"
		__sync_synchronize();
		pthread_yield();
	}

	//TIME for (int i = 0; i < tg->input.numOps; i++) {
	while(!stop){
		my_ops++;
	    int actionRand = simRandom() % 100 + 1;
	    int key = simRandom() % tg->input.elementsRange;
#ifndef HASH_OP
	    if (  actionRand < (tg->input.fractionDeletes * 100)  ) {
	        ListDelete(&entryHead, tg, key);
	    } else if (  actionRand < ((tg->input.fractionDeletes + tg->input.fractionInserts) * 100)  ) {
	        ListInsert(&entryHead, tg, key, 0);
	    } else {
	        ListSearch(&entryHead, tg, key, &tmpData);
	    }
#else
	    if (  actionRand < (tg->input.fractionDeletes * 100)  ) {
	        HashDelete(&hash, tg, key);
	    } else if (  actionRand < ((tg->input.fractionDeletes + tg->input.fractionInserts) * 100)  ) {
	        HashInsert(&hash, tg, key, 0);
	    } else {
	        HashSearch(&hash, tg, key, &tmpData);
	    }
#endif
	}

	__sync_fetch_and_add(&num_ops, my_ops);
	destroy_thread(tg);
	return 0;
}

unsigned long br=0;
__thread long cttr=0;
void barrier(int tid, int num_threads){
	assert( stop || (br/num_threads) == cttr);
	unsigned long ares = __sync_fetch_and_add(&br, 1);
	unsigned long res = ares/num_threads;
	while((br/num_threads)==res && !stop){__sync_synchronize();pthread_yield();}
	cttr++;
	if(!stop){
		assert( (br/num_threads) == res+1);
		assert( (br/num_threads)==cttr);
	}
}

#ifdef ALLOW_PINNING
pid_t gettid(void){
    return (pid_t) syscall(SYS_gettid);
}
void pin(pid_t t, int cpu)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    if(sched_setaffinity(t, sizeof(cpu_set_t), &cpuset))
    		exit(1);
}
#endif

///////DEBUGGING FUNCTIONS
int CountLL(Entry *head){
	int cnt=0;
	while(head!=NULL){
		cnt++;
		head=clearDeleted(head->nextEntry);
		if(cnt>10000000){
			printf("linked list is too long\n");
			assert(0);
		}
	}
	return cnt;
}
