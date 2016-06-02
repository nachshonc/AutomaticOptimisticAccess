#define _GNU_SOURCE
/*#ifndef NS
#error "must define namespace by define NS ..."
#define NCHILDS 1
#define CHILD1_OFFSET 0
#endif*/
#ifndef NS
#define NS
#define NCHILDS 0
#define CHILD1_OFFSET 0
#endif
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
#define _NAME_NAMESPACE(A,B) A ## B
#define NAME_NAMESPACE(A,B) _NAME_NAMESPACE(A,B)
#define NAME(A) NAME_NAMESPACE(A, NS)

//#define DISABLE_FREE
//#define DISABLE_GC
//#define PRINTGC printf
#define PRINTGC(...)
//#define ZERO_ALLOC Incorrect since checking the dirty bit must happen before clearing the object.
#define RECYCLE_NEEDED ((void*)0xFFFFFFFF)

static struct allocEntry *createEntry(void *mem, int size);
static struct allocEntry *pop(struct verEntry *adr, long ver);


void *NAME(palloc)(struct localAlloc *local){
	void *ret;
	PRINTF2("allocating. local=%p\n", local);
	start:
	if(local->alloc_cache!=NULL)
		if(local->alloc_cache->free > 0){
			ret = local->alloc_cache->ptrs[--local->alloc_cache->free];
			PRINTF2("allocate %p. cache=%p[%d]\n", ret,local->alloc_cache,
					local->alloc_cache->free);
#ifdef ZERO_ALLOC
			memset(ret, 0, local->global->entrySize);
#endif
			return ret;
		}
	//free(local->alloc_cache) //dont care about MM here.
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
		void *mem=malloc(ENTRIES_PER_CACHE*alc->entrySize);
		local->alloc_cache = createEntry((char*)mem, alc->entrySize);
		goto start;
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
	/*	int size = alc->entrySize;
     int totalSize = size*ENTRIES_PER_CACHE;//total size per cache
     int requiredSize = alc->numCaches; //we double the amount of memory. so we should allocate totalSize.
     struct allocEntry *head, *tmp;
     for(int i=0; i<alc->numCaches; ++i){
     tmp = head;
     head = allocEntry(size);
     head->next = tmp;
     }*/
	assert(0);
	//TODO: complete this function.
}

void *heap[5000]={NULL};//, *endheap;
static void initAllocator(struct allocator *alc, int entrySize, int numEntries,
		int numThreads, int HPsPerThread, struct DirtyRecord *dirties, struct localAlloc *lalloc){
	alc->numThreads=numThreads;
	alc->numHPEntries=numThreads*HPsPerThread;
	alc->dirties = dirties;
	alc->locals=lalloc;
	alc->entrySize = entrySize;
	//////allocating the heap
	int numChunks = 1 + ((numEntries*entrySize) / PAYLOAD_SIZE);
	//printf("numChunks = %d\n", numChunks);
	alc->phase.phase=INIT_PHASE;
	alc->sweep_counter=((unsigned long)INIT_PHASE<<32)|0;
	struct allocEntry *head=NULL, *tmp;
	char *curheap;
	//int counter = 0;
	for(int i=0; i<numChunks; ++i){
		assert(posix_memalign((void**)&curheap, CHUNK_SIZE, CHUNK_SIZE)==0);
		memset(curheap, 0, CHUNK_SIZE); 
		heap[i]=curheap;
		char *endchunk=curheap + CHUNK_SIZE;
		curheap = curheap + HEADER_SIZE;
		while(curheap+ENTRIES_PER_CACHE*entrySize <= endchunk){
			tmp = head;
			head = createEntry((char*)curheap, entrySize);
			//counter+=ENTRIES_PER_CACHE;
			head->next = tmp;
			curheap=(char*)curheap + ENTRIES_PER_CACHE*entrySize;
		}
	}
	//printf("%d chunks, %d entries\n",numChunks, counter);
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
	for(int i=0; i<MAX_CHUNKS; ++i)
		if(heap[i]!=NULL)
			free(heap[i]);
}

int alcctr=0;
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
	//__sync_fetch_and_add(&alcctr, 1);//count memory usage.
	return old.head;
}
