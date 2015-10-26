#include "Atomic.h"
#include "lfl.h"
#include "globals.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "allocator.h"
#include "simpleSet.h"
#include "rand.h"

#ifdef _assert
#undef _assert
#endif
#define _assert(B)

//#define DISABLE_FREE
//#define DISABLE_GC
//#define PRINTGC printf
#define PRINTGC(...)
#define ZERO_ALLOC
#define RECYCLE_NEEDED ((void*)0xFFFFFFFF)
#define UNFRREEZE(ptr) ((void*)((long)ptr&~1))
#define FREEZE(ptr) ((void*)((long)ptr|1))
void barrier(int tid, int num_threads);
int lk=0;

__thread Set hps=NULL;
Set Reclamation(struct allocator *global, struct localAlloc *local){
#ifdef DISABLE_GC
	printf("helpStartGc called. GC is disabled\n");
#else
    struct verEntry reclaim=global->reclaim, process;
    int localVer=local->localVer;
    struct verEntry newReclaim, newProcess;
    while(reclaim.ver==localVer){
    		newReclaim.head=reclaim.head;
    		newReclaim.ver=localVer+1;
    		if(verCAS(&global->reclaim, reclaim, newReclaim)){
    			reclaim.ver=localVer+1;
    			break;
    		}
    		reclaim=global->reclaim;
    }
    if(reclaim.ver==localVer+1){
		process=global->toProcess;
		while(process.ver==localVer){
			newProcess.head=reclaim.head;
			newProcess.ver=localVer+2;
			if(verCAS(&global->toProcess, process, newProcess))
				break;
			process = global->toProcess;
		}
		reclaim=global->reclaim;
		while(reclaim.ver==localVer+1){
				newReclaim.head=NULL;
				newReclaim.ver=localVer+2;
				if(verCAS(&global->reclaim, reclaim, newReclaim))
					break;
				reclaim=global->reclaim;
		}
    }
    local->localVer+=2;
    __sync_synchronize();

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
    __sync_synchronize();
    if(hps==NULL)
    		hps=setInit(global->numHPEntries*2);
    else
    		setReset(hps);
    Set HPs=hps;
    // Stage1 : Save current hazard pointers in HPlocal
    for(int i=0;  i < global->numThreads ;  ++i) {
        Entry* hptr0 = threadsDirties[i].HPs[0];
        Entry* hptr1 = threadsDirties[i].HPs[1];
        Entry* hptr2 = threadsDirties[i].HPs[2];
        if (hptr0 != NULL )
        		setAddP(HPs, hptr0);
        if (hptr1 != NULL )
        		setAddP(HPs, hptr1);
        if (hptr2 != NULL )
        		setAddP(HPs, hptr2);
    }
    return 0;
#endif
}

__attribute__((noinline)) struct allocEntry *pop(struct verEntry *adr, long ver){
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
	if(old.head->next==new.head) old.head->next=NULL;
	else if(old.head->next->next==new.head) old.head->next->next=NULL;
	else assert(0);
	return old.head;
}

int exist(void **arr, void *ptr){
	while(*arr!=NULL){
		if(*arr==ptr) return 1;
		arr++;
	}
	return 0;
}
void Recycle(struct allocator *global, struct localAlloc *local, Set HPs){
	while(1){
        int k,i;
		struct allocEntry *entry = pop(&global->toProcess, local->localVer), *nxt;
		if(entry==RECYCLE_NEEDED)
			return;
		PRINTGC("GC: popped cache entry %p. numEntries=%d\n", entry,(void*)((entry==NULL)?0:entry->free));
		if(entry==NULL) return;
		while(entry!=NULL){
			nxt=entry->next;
			for(i=0, k=0; i<entry->free; ++i){
				if(setContains(HPs, (int)((long)entry->ptrs[i])))//if(exist(HPs, entry->ptrs[i]))
					pfree(local, entry->ptrs[i]);
				else
					entry->ptrs[k++]=entry->ptrs[i];
			}
			entry->free=k;
			PRINTGC("returning back entry. k = %d\n", k);
			/*for(int i=0; i<k; ++i){//for debugging. zero out recycled objects.
				if(((ListNode)entry->ptrs[i])->valid==0)
					assert(0);
				memset(entry->ptrs[i], 0, global->entrySize);
			}*/
			if(k>0){
				//push entry to alloc
				struct allocEntry *tmp;
				do{
					tmp = global->head;
					entry->next = tmp;
				}while(!CAS(&global->head, tmp, entry));
			}
			entry=nxt;
		}
	}
}

void triggerCollection(struct allocator *global, struct localAlloc *local){
    PRINTGC("trigger collection. myver=%d\n", local->localVer);
#ifdef DISABLE_GC
    printf("gc is disabled. Aborted\n");
    assert(0);
#else
	//printCount(local, global);//to help debugging.
	Reclamation(global, local);
	PRINTGC("after Reclamation. ver=%d\n", local->localVer);
	Recycle(global, local, hps);
#endif
}

void *palloc(struct localAlloc *local){
	void *ret;
	PRINTF2("allocating. local=%p\n", local);
start:
    if(likely(local->alloc_cache!=NULL))
        if(likely(local->alloc_cache->free > 0)){
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
	struct allocEntry *next, *curhead;
	PRINTF2("allocating from global=%p\n", alc);
	do{
		do{
			curhead = alc->head;
			if(curhead==NULL) break;
			next = curhead->next;
		}while(!CAS(&alc->head, curhead, next));
		PRINTF2("allocating: popped entry =%p\n", curhead);
		if(curhead!=NULL){
			local->alloc_cache = curhead;
			local->alloc_cache->next=NULL;
			goto start;
/*			assert(local->alloc_cache->free > 0);
			ret = local->alloc_cache->ptrs[--local->alloc_cache->free];
			PRINTF2("allocate cache from global. entry= %p\n", ret);
#ifdef ZERO_ALLOC
		 	memset(ret, 0, alc->entrySize);
#endif
			return ret;*/
		}
#ifndef RoF
		assert(!"out of memory");
#endif
		triggerCollection(alc, local);
	}while(!stop);
	if(stop)//the threads starved, and the test finished. Just return something to finish nicely.
		return malloc(alc->entrySize);
	printf("OutOfMemory\n");
	assert(!!!"OutOfMemory");
	return NULL;
}

//a strange mechanism to "unalloc" an allocated node that remain local.
//To be used by HP or DropTheAnchor that want to use pool allocator.
void pReturnAlloc(struct localAlloc *local, void *obj){
	PRINTF2("pReturnAlloc. local=%p\n", local);
    struct allocator *alc = local->global;
    if(local->alloc_cache!=NULL){
        if(local->alloc_cache->free < ENTRIES_PER_CACHE){
            local->alloc_cache->ptrs[local->alloc_cache->free++] = obj;
            PRINTF2("pReturnAlloc %p. cache=%p[%d]\n", obj,local->alloc_cache,
            		local->alloc_cache->free);
            return;
        }
        struct allocator *alc = local->global;
        struct allocEntry *curalloc, *toalloc=local->alloc_cache;
        do{//pushing the full cache to ready nodes.
            curalloc = alc->head;
            toalloc->next = curalloc;
        }while(!CAS(&alc->head, curalloc, toalloc));
        local->alloc_cache=NULL;
    }
	local->alloc_cache = malloc(sizeof(struct allocEntry));
	memset(local->alloc_cache, 0, sizeof(struct allocEntry));
	local->alloc_cache->size = alc->entrySize;
	local->alloc_cache->free = 1;
    local->alloc_cache->ptrs[0]=obj;
}

void pfree(struct localAlloc *local, void *obj){
	PRINTF2("free object %p\n", obj);
#ifdef DISABLE_FREE
	return;
#else
    struct allocator *alc = local->global;
    struct allocEntry *tofree;
    if(obj==NULL) return;
restart:
    if(local->free_cache!=NULL){
        if(local->free_cache->free < ENTRIES_PER_CACHE){
            local->free_cache->ptrs[local->free_cache->free++] = obj;
            return;
        }

        tofree = local->free_cache;
        struct verEntry reclaim, newitem;
        do{
        		reclaim = alc->reclaim;
        		if(reclaim.ver != local->localVer){
        			triggerCollection(alc, local);
                goto restart;
            }
            tofree->next = reclaim.head;
        		newitem.head=tofree;
        		newitem.ver=local->localVer;
        }while(!verCAS(&alc->reclaim, reclaim, newitem));
    }
	local->free_cache = malloc(sizeof(struct allocEntry));
	DEB(memset(local->free_cache, 0, sizeof(struct allocEntry)));
	local->free_cache->size = alc->entrySize;
	local->free_cache->free = 1;
    local->free_cache->ptrs[0]=obj;
#endif
}

struct allocEntry *allocEntry(int size){
	int totalSize = size*ENTRIES_PER_CACHE;//total size per alloc_cache
	struct allocEntry *head = malloc(sizeof(struct allocEntry));
	void *mem = malloc(totalSize);
	memset(mem, 0, totalSize);
	for(int j=0; j<ENTRIES_PER_CACHE; ++j){
		head->ptrs[j] = ((char*)mem + j*size);
	}
	head->size = size;
	head->next = NULL;
	head->free = ENTRIES_PER_CACHE;
	return head;
}
void getMoreSpace(struct allocator *alc){
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
	//WHAT SHOULD I DO?/
}
void initAllocator(struct allocator *alc, int entrySize, int numEntries,
		int numThreads, int HPsPerThread, struct DirtyRecord *dirties, struct localAlloc *lalloc){
	alc->numThreads=numThreads;
	alc->numHPEntries=numThreads*HPsPerThread;
	alc->dirties = dirties;
	alc->locals=lalloc;
	alc->entrySize = entrySize;

	alc->numCaches = 1 + numEntries / ENTRIES_PER_CACHE;
	alc->HP=NULL;
	//alc->numThreads=5;
	alc->reclaim.ver=2;alc->reclaim.head=NULL;
	alc->toProcess.ver=2; alc->toProcess.head=NULL;
	struct allocEntry *head=NULL, *tmp;
	for(int i=0; i<alc->numCaches; ++i){
		tmp = head;
		head = allocEntry(entrySize);
		head->next = tmp;
	}
	alc->head = head;
	//printf("allocated %d entries\n",cglob(alc));
}

void initLocalAllocator(struct localAlloc *local, struct allocator *allocator, int tid){
	local->global=allocator;
	local->alloc_cache=NULL;
	local->free_cache=NULL;
	local->localVer=INIT_PHASE;
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
void destroyAllocator(struct allocator *alc){}
int cals(struct allocEntry *h){
	int ctr=0;
	while(h){
		ctr+=h->free;
		h=h->next;
	}
	return ctr;
}
int cglob(struct allocator *global){
	return cals(global->reclaim.head)+cals(global->toProcess.head)+cals(global->head);
}
void printCount(struct localAlloc *local, struct allocator* global){
    barrier(local->tid, global->numThreads);
    int lc = ((local->alloc_cache)?local->alloc_cache->free:0)+((local->free_cache)?local->free_cache->free:0);
    int gc=cglob(global);
    //int Count(hash);
    int slc=0;//Count(hash);
    if(global->numThreads!=1)
    		printf("the function was designed for single threaded program. Please improve it.\n");
    printf("accounted for %d\n", lc+gc+slc);
    barrier(local->tid, global->numThreads);
}

void swap(void *a[], int ia, void *b[], int ib){
	void *tmp=a[ia];
	a[ia]=b[ib];
	b[ib]=tmp;
}
//a crude randomization algorithm for the cache entries.
//For the linked list we sort the items. When using normal allocation caches, the linked list looks
//like an array.
void randAllocator(struct allocator *alc, int numEntries){
	int numCaches = numEntries / ENTRIES_PER_CACHE;
	struct allocEntry *caches[numCaches];
	struct allocEntry *cur=alc->head;
	for(int i=0; i<numCaches && cur!=NULL; ++i){
		caches[i]=cur;
		cur=cur->next;
	}
	for(int i=0; i<numCaches-1; ++i)
		for(int j=0; j<ENTRIES_PER_CACHE; j++){
			int cidx = numCaches-1- (simRandom()%(numCaches-i-1));
			int iidx = simRandom()%ENTRIES_PER_CACHE;
			swap(caches[i]->ptrs, j, caches[cidx]->ptrs, iidx);
		}
}

#ifdef DEBUGGING
int main(){
	struct allocator global={0};
    global.dirties = NULL;
	initAllocator(&global, 32, 8);
	struct localAlloc loc1={0}, loc2={0};
	loc1.global=&global;
	loc2.global=&global;
    int k=0;
	for(int i=0; i<6; ++i){
		void *val = palloc(&loc1);
		printf("val=%p\n", val);
		pfree(&loc2, val);
        HPArray[k++]=val;
	}
	for(int i=0; i<2; ++i){
		void *val = palloc(&loc2);
		printf("val=%p\n", val);
		pfree(&loc1, val);
	}
	void *val = palloc(&loc1);
	printf("**val = %p\n", val);
	val = palloc(&loc1);
	printf("**val = %p\n", val);
	val = palloc(&loc1);
	printf("**val = %p\n", val);

}

#endif
