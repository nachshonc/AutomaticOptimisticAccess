#define _GNU_SOURCE
#ifndef NS
#define NS //make the Eclipse compiler happy...
#error "must define name-space by #define NS ..."
#define NCHILDS 1
#define CHILD1_OFFSET 0
#endif
#define _NAME_NAMESPACE(A,B) A ## B
#define NAME_NAMESPACE(A,B) _NAME_NAMESPACE(A,B)
#define NAME(A) NAME_NAMESPACE(A, NS)

#include "Atomic.h"
#include "lfl.h"
#include "globals.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "allocator.h"
#include "rand.h"
#include "lfhash.h"
#include "debugging.h"
#include "trace.c"

//#define DISABLE_FREE
//#define DISABLE_GC
//#define PRINTGC printf
#define PRINTGC(...)
//#define ZERO_ALLOC Incorrect since checking the dirty bit must happen before clearing the object.
#define RECYCLE_NEEDED ((void*)0xFFFFFFFF)

__thread void **NAME(HPs)=NULL;

static int Reclamation(struct allocator *global, struct localAlloc *local, void **HPlocal){
#ifdef DISABLE_GC
	printf("helpStartGc called. GC is disabled\n");
#else
	CAS(&global->phase.phase, local->localVer, local->localVer+2);
	int localVer=local->localVer;
	struct verEntry oldhead = global->head, newhead;
	newhead.head=NULL;
	newhead.ver=localVer+2;
	while(oldhead.ver==local->localVer){
		verCAS(&global->head, oldhead, newhead);
		oldhead=global->head;
	}
	local->localVer+=2;
	if(global->phase.phase!=local->localVer) return -1; //Someone already started next phase.
    const unsigned long dNp=(unsigned long)local->localVer<<8, dNpd=dNp|1ul;
    for(int i=0; i<global->numThreads; ++i){
    		//updating the dirty bit of thread i.
    		unsigned long dNpTi = global->dirties[i].dirtyNphase;
    		if(dNpTi>=dNp) continue;
    		CAS(&global->dirties[i].dirtyNphase, dNpTi, dNpd);
		//in case the first CAS failed due to the thread clearning it bits.
    		dNpTi = global->dirties[i].dirtyNphase;
    		if(dNpTi>=dNp) continue;
    		CAS(&global->dirties[i].dirtyNphase, dNpTi, dNpd);
    }
	int k=0;
	// Stage1 : Save current hazard pointers in HPlocal
	for(int i=0;  i < global->numThreads ;  ++i) {
		Entry* hptr0 = threadsDirties[i].HPs[0];
		Entry* hptr1 = threadsDirties[i].HPs[1];
		Entry* hptr2 = threadsDirties[i].HPs[2];
		if (hptr0 != NULL )
			HPlocal[k++] = hptr0;
		if (hptr1 != NULL )
			HPlocal[k++] = hptr1;
		if (hptr2 != NULL )
			HPlocal[k++] = hptr2;
	}
	return k;
	PRINTF("found %d non-null entries in HP\n", k);
#endif
}

static struct allocEntry *pop(struct verEntry *adr, long ver);
static void freeAllocEntry(struct allocEntry *p);
static void triggerCollection(struct localAlloc *local){
	PRINTGC("trigger collection. myver=%d\n", local->localVer);
#if defined(DISABLE_GC) || !defined(OA)
	printf("gc is disabled. Aborted\n");
	assert(0);
#endif
	struct allocator *global = local->global;
	//barrier(local->tid, global->numThreads);
	if(local->alloc_cache!=NULL)
		freeAllocEntry(local->alloc_cache);
	local->alloc_cache=NULL;

	void **HPlocal = (NAME(HPs))?(NAME(HPs)):(NAME(HPs)=malloc((global->numHPEntries+1)*sizeof(void*)));
	int hps = Reclamation(global, local, HPlocal);
	if(hps==-1) return; //phase already finished.

	clearMarks(local, local->localVer);
	clearRootsMark(local);
	trace(HPlocal, hps, local);
	DCHECK(DNHASHOP(verify(local)));
	DCHECK(DHASHOP(verifyH(local)));

	//////SWEEPING
	unsigned long gsc = global->sweep_counter;
	assert(PHASE(gsc)>=local->localVer);
	sweep(local);

}
static void helpCollection(struct localAlloc *local){
	if(local->localVer<local->global->phase.phase)
		triggerCollection(local);
}
void *NAME(palloc)(struct localAlloc *local){
	void *ret;
	PRINTF2("allocating. local=%p\n", local);
	start:
	if(local->alloc_cache!=NULL){
		if(local->alloc_cache->free > 0){
			ret = local->alloc_cache->ptrs[--local->alloc_cache->free];
			PRINTF2("allocate %p. cache=%p[%d]\n", ret,local->alloc_cache,
					local->alloc_cache->free);
#ifdef ZERO_ALLOC
			memset(ret, 0, local->global->entrySize);
#endif
			return ret;
		}
		freeAllocEntry(local->alloc_cache);
		local->alloc_cache=NULL;
	}
	struct allocator *alc = local->global;
	struct allocEntry *curhead;
	PRINTF2("allocating from global=%p\n", alc);
	int iter = 0;
	do{
		curhead = pop(&alc->head, local->localVer);
		PRINTF2("allocating: popped entry =%p\n", curhead);
		if(curhead!=NULL && curhead!=RECYCLE_NEEDED){
			local->alloc_cache = curhead;
			local->alloc_cache->next=NULL;
			goto start;
		}
		triggerCollection(local);
		if(alc->dirties[local->tid].dirty){
			return NULL;//Discarded by the check after alloc.
		}
		assert(local->alloc_cache==NULL);
	}while(iter++ < 100);
	printf("OutOfMemory.\n");
	assert(!!!"OutOfMemory");
	return NULL;
}

static struct allocEntry *createEntry(void *mem, int size){
	struct allocEntry *head = malloc(sizeof(struct allocEntry));
	for(int j=0; j<ENTRIES_PER_CACHE; ++j){
		head->ptrs[j] = ((char*)mem + j*size);
	}
	head->size = size;
	head->next = NULL;
	head->free = ENTRIES_PER_CACHE;
	return head;
}
static void getMoreSpace(struct allocator *alc){
	assert(0);
	//TODO: complete this function. (Note, in general allocator is not lock-free, so I avoided it).
}

//----------------------------allocEntry management.
/* Memory management is done using IBM tags with phase number.
 * Consider a single phase and an allocEntry E.
 * During the sweep, E is possibly inserted into the shared allocation pool.
 * During the execution, E is possibly popped by an allocating thread.
 * Both these operations may success only during the current phase, because accesses
 * to the shared pool is protected by the phase number.
 * ABA is possible: suppose T1 want to pop E. Then T2 adds another element and T3 remove it.
 * Then T1 is actually successful. But this is not a problem because it is correct to extract E.
 */
struct allocEntry NAME(allocationEntriesCache)[100000];
int NAME(allocationEntriesCacheHead)=0;
__thread struct allocEntry *NAME(entriesLocalFreeList);
static struct allocEntry *getAllocEntry(){
	struct allocEntry *p=NAME(entriesLocalFreeList);
	if(p!=NULL){
		NAME(entriesLocalFreeList)=p->next;
		return p;
	}
	//allocate new entry from pool
	int aec = __sync_fetch_and_add(&NAME(allocationEntriesCacheHead), 1);
	assert(aec<(sizeof(NAME(allocationEntriesCache))/sizeof(struct allocEntry)));
	return &NAME(allocationEntriesCache)[aec];
}
static void freeAllocEntry(struct allocEntry *p){
	p->next=NAME(entriesLocalFreeList);
	NAME(entriesLocalFreeList)=p;
}

/////////////////////--------------------Handle heap layout (allocator)
void *NAME(heapmem)[MAX_CHUNKS]; //chunks of memory
int NAME(heapmemcnt)=0;  //number of chunks currently in use

static void initAllocator(struct allocator *alc, int entrySize, int numEntries,
		int numThreads, int HPsPerThread, struct DirtyRecord *dirties, struct localAlloc *lalloc){
	alc->numThreads=numThreads;
	alc->numHPEntries=numThreads*HPsPerThread;
	alc->dirties = dirties;
	alc->locals=lalloc;
	alc->entrySize = entrySize;
	//////allocating the heap
	int numChunks = 1 + ((numEntries*entrySize) / PAYLOAD_SIZE);
	NAME(heapmemcnt) = numChunks;
	for(int i=0; i<numChunks; ++i){
		assert(posix_memalign(&NAME(heapmem)[i], CHUNK_SIZE, CHUNK_SIZE)==0);
		memset(NAME(heapmem)[i], 0, CHUNK_SIZE);
		assert( ((long)NAME(heapmem)[i] & (CHUNK_SIZE-1)) ==0 );
	}
	alc->phase.phase=INIT_PHASE;
	alc->sweep_counter=((unsigned long)INIT_PHASE<<32)|0;
	struct allocEntry *head=NULL, *tmp;
	for(int i=0; i<numChunks; ++i){
		char *curheap = NAME(heapmem)[i];
		char *endchunk=curheap + CHUNK_SIZE;
		curheap = curheap + HEADER_SIZE;
		while(curheap+ENTRIES_PER_CACHE*entrySize <= endchunk){
			tmp = head;
			head = createEntry((char*)curheap, entrySize);
			head->next = tmp;
			curheap=(char*)curheap + ENTRIES_PER_CACHE*entrySize;
		}
	}
	alc->head.head = head;
	alc->head.ver=INIT_PHASE;
}
static void initLocalAllocator(struct localAlloc *local, struct allocator *allocator, int tid){
	local->global=allocator;
	local->alloc_cache=NULL;
	local->free_cache=NULL;
	local->localVer=INIT_PHASE;
	local->markstack=malloc(sizeof(void*)*MARKSTACK_SIZE);
	memset(local->markstack, 0, sizeof(void*)*MARKSTACK_SIZE);
	local->markstack[0]=(void*)1;//start with a finished state.
	local->tid=tid;
}
void init_allocator(struct allocator *allocator, struct localAlloc *lalloc, int numThreads,
	struct DirtyRecord *dirties, int numEntries, int HPsPerThread){
	memset(dirties, 0, numThreads*sizeof(struct DirtyRecord));
	memset(lalloc, 0, numThreads*sizeof(struct localAlloc));
	threadsDirties=dirties;
	initAllocator(allocator, sizeof(Entry), numEntries, numThreads, HPsPerThread, dirties, lalloc);
	for(int i=0; i<numThreads; ++i){
		initLocalAllocator(&lalloc[i], allocator, i);
		dirties[i].dirtyNphase=INIT_PHASE<<8;
	}
}
void destroyAllocator(struct allocator *alc){
	for(int i=0; i<NAME(heapmemcnt); ++i){
		free(NAME(heapmem)[i]);
		NAME(heapmem)[i]=NULL;
	}
	NAME(heapmemcnt)=0;
}
static int assert_in_heap(void *ptr){
	ptr = (void*)((long)ptr&~(long)(CHUNK_SIZE-1));
	for(int i=0; i<NAME(heapmemcnt); ++i)
		if(ptr==NAME(heapmem)[i])
			return 1;
	assert(!"ptr is not valid");
	return 0;
}
static struct allocEntry *pop(struct verEntry *adr, long ver){
	struct verEntry old, new;
	do{
		old = *adr;
		if(old.ver!=ver)
			return RECYCLE_NEEDED;
		if(old.head==NULL)
			return NULL;
		new.ver=old.ver;
		new.head=old.head->next;
	}while(!verCAS(adr, old, new));
	return old.head;
}

