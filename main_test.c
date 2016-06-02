#define DEFAULT_TIME 1
#define DEFAULT_SEARCH_FRACTION 0.80
#define DEFAULT_SET_SIZE 10000
#ifndef HASH_OP
#undef DEFAULT_SET_SIZE
#define DEFAULT_SET_SIZE 5000
#endif
#define DISPLAY_PARAMS 1
#define MAX_THREADS 128
int HEAP_SIZE=20000;

#include "lfl.h"
#include "lfhash.h"
#include "worker.h"
#include "Atomic.h"
#include "allocator.h"
#include "rand.h"
#include <assert.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include "debugging.h"
const int HPsPerThread=3;
long num_ops=0;
volatile Bool run = FALSE, stop = FALSE;
void initialize_ds(int initListSize, int elementsRange, ThreadGlobals *tg);
Input parseArgs(int argc, char *argv[], int *pNumThreads, int *ptime, int *pInitListSize, int *pRange);

ThreadGlobals tg[MAX_THREADS];
struct DirtyRecord dirties[MAX_THREADS];
struct localAlloc lalloc[MAX_THREADS];
pthread_t workerThreads[MAX_THREADS];

void init_allocator(struct allocator *allocator, struct localAlloc *lalloc, int numThreads,
	struct DirtyRecord *dirties, int numEntries, int HPsPerThread);
int main(int argc, char *argv[]) {
	int numThreads, time, initListSize, elementsRange;
	Input input = parseArgs(argc, argv, &numThreads, &time, &initListSize, &elementsRange);

	struct allocator allocator;
	int allocSize = HEAP_SIZE;
#if !defined(OA)
	allocSize = 6000000;//15000000
#elif defined(MOA)
	allocSize=(numThreads>32 || (numThreads==32 && input.fractionInserts>0.2))?50000:allocSize; //for 32 threads a heap of 20000 items is too small.
#endif
	init_allocator(&allocator, lalloc, numThreads, dirties, allocSize, HPsPerThread);
#ifdef HASH_OP
	int logLen=0, size=(int)(initListSize/LOAD_FACTOR);
	while(size/=2) logLen++;
	assert(logLen!=0);//don't know why, but the HASH don't work well.
	if(logLen<9) logLen=9;
	assert(1<<logLen >= ARR_ENTRIES_PER_BIT);
	initHash(&hash, logLen);
#endif

	for (int i = 0; i < numThreads; i++) {
		tg[i].input = input;
		tg[i].input.threadID		= i;
		tg[i].dirty = dirties+i;
		tg[i].entryAllocator = lalloc+i;
	} // end of for loop, initializing the threads data

	initialize_ds(initListSize, elementsRange, tg);

	for (int i = 0; i < numThreads; i++) {
		if(pthread_create(&workerThreads[i], NULL, start_routine, &tg[i])){
			printf("Error occurred when creating thread %d\n", i);
			exit(1);
		}
	}
	////////////START TEST
	run = TRUE; __sync_synchronize();
	sleep(time);
	stop=TRUE;  __sync_synchronize();
	////////////END TEST

	for (int i=0; i< numThreads; ++i) {			// join all threads
		pthread_join(workerThreads[i], NULL);
	}
	//TIME g_timer_stop(t);

#ifndef MALLOC
	destroyAllocator(&allocator);
#endif
#if defined(OA)
	int numPhases = allocator.phase.phase/2-INIT_PHASE;
#else
	int numPhases = 0;
#endif
	num_ops = (long long)num_ops / (long double)time;
	const double M=1000000;
	printf(HG_VER "Threads=%d, Thpt=%ld, ThptM=%.1f, Phases=%d, Time=%f, InitSize=%d, Range=%d, SearchFrac=%.2f\n",
			numThreads, num_ops, num_ops/M, numPhases, (double)time, initListSize, elementsRange,
			1-tg[0].input.fractionInserts-tg[0].input.fractionDeletes);
	printf("___ %ld %d %.2f\n", num_ops, numThreads, 1-tg[0].input.fractionInserts-tg[0].input.fractionDeletes);
#if !defined(MOA) && !defined(OA)
extern int alcctr;
printf("alcctr = %d\n", alcctr);
//extern int m;
//printf("m =%d\n", m);
#endif
	//TIME printf("___ %f %d %.2f\n", totalTime, numThreads, 1 - atof(argv[6]) - atof(argv[5]));
	return 0;
}

Input parseArgs(int argc, char *argv[], int *pNumThreads, int *ptime,
		int *pInitListSize, int *pRange){
	if (argc < 2) {
		printf("format is: %s num_threads [set_size search_fraction time heap_size]\n", argv[0]);
		//printf("bad arguments, format is: %s num_threads num_ops init_list_size elem_range ins_ops [0..1] del_ops [0..1] \n", argv[0]);
		exit(0);
	}
	if(strcmp(argv[1], "-h")==0 || strcmp(argv[1], "--help")==0){
		printf("Usage: %s num_threads [set_size search_fraction time heap_size]\n", argv[0]);
		printf("First argument is mandatory, other four are optional\n"); 
		printf("set_size\t the average number of items in the set. Range is set_size*2. Default=%d\n", DEFAULT_SET_SIZE); 
		printf("search_fraction\t the fraction of search operation. Defualt: %.2f\n", DEFAULT_SEARCH_FRACTION); 
		printf("time\t\t the time in seconds. Default: %d\n", DEFAULT_TIME); 
		printf("heap_size\t the memory available for allocations. This represents the stress on the memory manager. Default: %d\n", 
									HEAP_SIZE); 
		exit(0); 
	}
	*pNumThreads		= atoi(argv[1]);

	*pInitListSize	=DEFAULT_SET_SIZE;
	if(argc>2 && atoi(argv[2])!=0)
		*pInitListSize = atoi(argv[2]);
	*pRange = 2*(*pInitListSize);

	float search_fraction=DEFAULT_SEARCH_FRACTION;
	if(argc>3)
		search_fraction = atof(argv[3]);
	assert(search_fraction>=0 && search_fraction<=1);

	*ptime = DEFAULT_TIME;
	if(argc>4 && atoi(argv[4])!=0)
		*ptime			= atoi(argv[4]);

	if(argc>5 && atoi(argv[5])!=0)
		HEAP_SIZE = atoi(argv[5]);

	if(DISPLAY_PARAMS)
		printf("PARAMS: threads=%d, set_size=%d, range=%d, operations=%.1f-%.1f-%.1f, "
				"time=%d, Heap=%d\n",
				*pNumThreads,*pInitListSize, *pRange, search_fraction,
				(1-search_fraction)/2, (1-search_fraction)/2, *ptime, HEAP_SIZE);
	Input ret;
	ret.threadNum=*pNumThreads;
	ret.fractionDeletes=(1-search_fraction)/2;
	ret.fractionInserts = (1-search_fraction)/2;
	ret.elementsRange = *pRange;
	return ret;
}

/////////////////////////////////////FAST INITIALIZATION OF DS.
//for initialization of the data structure before the actual test.
static Bool findFAST(Entry** entryHead, ThreadGlobals* tg, int key, ThreadLocal *fres) {
	int ckey;
	fres->prev = entryHead;
	fres->cur = *(fres->prev);
	while (fres->cur != NULL) {
		fres->next = fres->cur->nextEntry;
		ckey = getKey(fres->cur->keyData);
		if (ckey >= key)return (ckey == key);
		fres->prev = &(fres->cur->nextEntry);
		fres->cur = fres->next;
	}
	return FALSE;
}
Entry *mid=NULL;
int midkey;
static Bool ListInsertFAST(Entry** entryHead, ThreadGlobals* tg, int key, Data data) {
#ifndef HASH_OP
	if(mid!=NULL && entryHead!=&mid->nextEntry && key>(int)mid->keyData)
		return ListInsertFAST(&mid->nextEntry, tg, key, data);
#endif
	Entry* newEntry = NULL;
	ThreadLocal fres;
	while (TRUE) {
		if (findFAST(entryHead, tg,key, &fres)) {
			return FALSE; //key exists
		}
		//create entry
#ifdef MALLOC
		newEntry = (Entry*)malloc(sizeof(Entry));
#else
		newEntry = (Entry*)palloc(tg->entryAllocator);
#endif
		newEntry->keyData = key;
		newEntry->nextEntry = fres.cur;
		newEntry->refcount=2;
		*fres.prev=newEntry;
#ifndef HASH_OP
		if(key<midkey && (mid==NULL || key>(int)mid->keyData))
			mid=newEntry;
#endif
		return TRUE;
	}
}
Bool HashInsertFAST(HashMap *hash, ThreadGlobals* tg, int key, Data data) {
	int bucket = HASH(key, hash->logLen);
	assert(bucket>=0 && bucket<hash->len);
	return ListInsertFAST(&hash->array[bucket], tg, key, data);
}
void initialize_ds(int initListSize, int elementsRange, ThreadGlobals *tg){
#if defined(DAOA) && !defined(HASH_OP)
	void addRoot(void **root, int len);
	addRoot((void**)&entryHead, 1);
#endif
	entryHead=NULL;
	midkey=elementsRange/2;
	simSRandom(-1);
	for (int i = 0; i < initListSize; i++) {
		int key = simRandom() % elementsRange;
#ifndef HASH_OP
		ListInsertFAST(&entryHead, tg, key, 0);
#else
		HashInsertFAST(&hash, tg, key, 0);
#endif
	}
}

#ifdef MICHAEL_ALLOCATOR
1. Define the heap. Make sure to call init_heap before executing anything that requires allocation.
2. Make sure that each thread (worker) calls void mono_thread_attach (void); before it start working.
3. Change malloc/palloc to ppalloc() (defined later), and pfree to ppfree (defined later).

Compilation: goto allocator. make. remove test.o
Add to makefiles to linked flags: allocator/ *.o
#define MONO_INTERNAL
#include "allocator/lock-free-alloc.h"
//#include "allocator/mono-linked-list-set.h"
MonoLockFreeAllocSizeClass test_sc;
MonoLockFreeAllocator test_heap;
void init_heap (void)
{
	void mono_thread_smr_init (void);
	void mono_thread_attach (void);
	mono_thread_smr_init();
	mono_thread_attach();
	mono_lock_free_allocator_init_size_class (&test_sc, 32);
	mono_lock_free_allocator_init_allocator (&test_heap, &test_sc);
	mono_lock_free_alloc (&test_heap);
	printf("heap initialized\n");
}
void *ppalloc(){return mono_lock_free_alloc(&test_heap);}
void ppfree(void *ptr){mono_lock_free_free(ptr);}
#endif
