/*
 * allocator.h
 *
 *  Created on: Jun 2, 2014
 *      Author: nachshonc
 */

#ifndef ALLOCATOR_H_
#define ALLOCATOR_H_
#define HG_VER "1.0 "
//#define MORE_CHECKS

#include "Atomic.h"
#include <string.h>
#define ENTRIES_PER_CACHE 126
#define CHUNK_SIZE (1<<19)
#define NODE_SIZE 32
#define MEM_PER_BIT (NODE_SIZE/2)
#define MEM_PER_BYTE (MEM_PER_BIT*8)
#define HEADER_SIZE_USED ((CHUNK_SIZE)/(MEM_PER_BYTE))
#define ROUND_UP(N, S) ((((N) + (S) - 1) / (S)) * (S))
#define HEADER_SIZE (ROUND_UP(HEADER_SIZE_USED, NODE_SIZE))
#define PAYLOAD_SIZE ((CHUNK_SIZE)-(HEADER_SIZE))
#define SWEEP_PAGE ((1<<16))
#define SWEEP_PAGES_PER_CHUNK (CHUNK_SIZE/SWEEP_PAGE)
#define MAX_CHUNKS 20 //support at most 10M
#define MAX_NUMBER_OF_ROOTS 10

#define MARKSTACK_SIZE (1000)
#define ARR_ENTRIES_PER_BIT 64
#define BITS_PER_CHAR 8
typedef unsigned long u64;
//each u64 is 64 bits, but each two bits have ARR_ENTRIES_PER_BIT, so total is ENTRIES_PER_WORD
#define ENTRIES_PER_WORD ((ARR_ENTRIES_PER_BIT)*32)

#define INIT_PHASE (0ul)

struct DirtyRecord{
	union{
		char dirty;
		long dirtyNphase;
	};
	void *HPs[7];
}__attribute__((aligned(64)));

struct allocEntry{
	struct allocEntry *next;
	int size;//size of each entry in
	int free;//points to the next empty entry
	void *ptrs[ENTRIES_PER_CACHE];
};

struct verEntry{
	struct allocEntry *head;
	long ver;
}; //__attribute__((aligned(16)));
struct phase{
	long phase;
}__attribute__((aligned(64)));
static inline int verCAS(struct verEntry *mem, struct verEntry old, struct verEntry new){
	DWORD oldval, newval;
	memcpy(&oldval, &old, sizeof(DWORD));
	memcpy(&newval, &new, sizeof(DWORD));
	return DWCAS((DWORD*)mem, oldval, newval);
}
struct localAlloc;
struct allocator{
	struct phase phase;
#ifndef MOA
	struct verEntry head;
#else //manual OA
	struct verEntry reclaim, toProcess;
	struct allocEntry *head;
#endif
	int entrySize;
	int numCaches; //number of caches in this allocator
	struct DirtyRecord *dirties;
	struct localAlloc *locals;
	int numThreads;
	int numHPEntries; //number of entries per thread.
	unsigned long sweep_counter; //lower 32 bits are counter. higher are phase version.
	//deprecated
	void **HP;
	void **HPhead;//the head of the hazard pointer array.
};
struct localAlloc{
	struct allocator *global;
	struct allocEntry *alloc_cache, *free_cache;
	long localVer;
	void **markstack;
	int tid;
}__attribute__((aligned(64)));
static void initAllocator(struct allocator *alc, int entrySize, int numEntries,
		int numThreads, int HPsPerThread, struct DirtyRecord *dirties, struct localAlloc *lalloc);
static void initLocalAllocator(struct localAlloc *local, struct allocator *, int tid);
void destroyAllocator(struct allocator *alc);
void pfree(struct localAlloc *local, void *obj);
void *palloc(struct localAlloc *local);

static void helpCollection(struct localAlloc *local);

static void sweep(struct localAlloc *local);
static void trace(void *array[], int len, struct localAlloc *local);
void dumpMarks();
static void clearMarks(struct localAlloc *local, unsigned long phaseNum);
static void clearArrMarks(struct localAlloc *local, unsigned long phaseNum, void *markbits, int len);
static struct allocEntry *getAllocEntry();
static int assert_in_heap(void *ptr);

//for parsing sweep_counter
#define PHASE(sweep_counter) ((unsigned)(sweep_counter>>32))
#define SCOUNTER(sweep_counter)  ((unsigned)sweep_counter)
#define MAKE_SC(cnt, phase) (((unsigned long)phase<<32)| (unsigned long)cnt)

//8 stands for sizeof(unsigned long)
#if (HEADER_SIZE >= (MEM_PER_BYTE*8))
#define DALLOW_SMALL_SP(S)
#else
#define DALLOW_SMALL_SP(S) S
#endif
#if defined(MORE_CHECKS)
#define DCHECK(S) S
#else
#define DCHECK(S)
#endif

//is data reside in current phase?
//@data the data. the phase number resides in the higher 32 bits.
//@PhaseShifted the phase shifted by 32.
static inline int is_cur_phase(unsigned long data, unsigned long phaseShifted){
	return (data&0xFFFFFFFF00000000ull)==phaseShifted;
}

#endif /* ALLOCATOR_H_ */
