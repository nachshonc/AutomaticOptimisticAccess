#include "allocator.h"
#include "globals.h" //for dirtyRecord.
#include "lfl.h"
#include "lfhash.h"
#include <assert.h>
#include <stdlib.h>
#include "debugging.h"
int usleep(unsigned int);
extern void *NAME(heapmem)[MAX_CHUNKS];
extern int NAME(heapmemcnt);

#define HEADER_MASK ((long)(~(CHUNK_SIZE-1l)))
#define OFFSET_MASK ((long)(CHUNK_SIZE-1))
#ifndef UNMARK
#define UNMARK(obj) ((void*)(((long)(obj))&(~3ul)))
#endif
#define CLEAR_PTR(obj) (UNMARK((obj)))

static void finishTraceArr(struct localAlloc *local, void ** arr,int len,u64* bits,
		void **markstack,u64 localSweep,	const unsigned long *const globalSweepPtr);

typedef unsigned long mtype;
#define BITS_PER_MTYPE (sizeof(mtype)*8)
static inline void get_mark_params(void *ptr, mtype **out_h_ptr,
		mtype *out_mask){
	void *base = (void*)((long)ptr & HEADER_MASK);
	int offset = (long)ptr & OFFSET_MASK;
	offset/=NODE_SIZE;
	*out_h_ptr = (mtype*)base + offset/(BITS_PER_MTYPE/2);
	*out_mask= (mtype)1<<(offset&(BITS_PER_MTYPE/2-1));
}
static inline int is_marked(void *ptr){
	mtype *h_ptr, mask;
	get_mark_params(ptr, &h_ptr, &mask);
	return (*h_ptr)&mask;
}
//@localPhase: the phase reside at the highest 32 bits. The other bits are zeros.
static inline int __mark(mtype *h_ptr, mtype mask, mtype old,unsigned long localPhase){
	if(unlikely(!is_cur_phase(old, localPhase)))
		return 0;
	if(CAS(h_ptr, old, old|mask)) return 1;
	do{
		old=*h_ptr;
		if(old&mask) return 0;
		//should be optimized for the common case..
		if(!is_cur_phase(old, localPhase))
			return 0;
	}while(!CAS(h_ptr, old, old|mask));
	return 1;
}

//@localPhase: the phase reside at the highest 32 bits. The other bits are zeros.
static inline int mark(void *ptr, unsigned long localPhase){
	mtype *h_ptr, mask;
	get_mark_params(ptr, &h_ptr, &mask);
	mtype old = *h_ptr;
	if(old&mask) return 0;
	return __mark(h_ptr, mask, old, localPhase);
}
//--------------------------------------------------handle global roots.
void **roots[MAX_NUMBER_OF_ROOTS];
void *arr_marktable[MAX_NUMBER_OF_ROOTS];
int rootLen[MAX_NUMBER_OF_ROOTS];
int NAME(currentRoot)=0;
void NAME(addGlobalRoot)(void **root, int len){
	int idx = __sync_fetch_and_add(&NAME(currentRoot), 1);	assert(idx<MAX_NUMBER_OF_ROOTS);
	roots[idx]=root;
	rootLen[idx]=len;
	if(len>1){
		//allocate marktable for the array, and an additional synchronization counter.
		int array_marktable_size = (len+ENTRIES_PER_WORD-1)/(ENTRIES_PER_WORD)*sizeof(u64);
		arr_marktable[idx]=malloc(array_marktable_size + sizeof(u64));
		memset(arr_marktable[idx], 0, array_marktable_size + sizeof(u64));
	}
}
static inline void clearRootsMark(struct localAlloc *local){
	for(int i=0; i<NAME(currentRoot); ++i){
		if(rootLen[i]!=1){
			int array_marktable_size = (rootLen[i]+ENTRIES_PER_WORD-1)/(ENTRIES_PER_WORD)*sizeof(u64);
			clearArrMarks(local, local->localVer, arr_marktable[0], array_marktable_size+sizeof(u64));
		}
	}
}

static void traceArr(void **arr, const int len, void *base, struct localAlloc *local);
static void traceRoots(struct localAlloc *local, int *pcounter){
	for(int i=0; i<NAME(currentRoot); ++i){
		if(rootLen[i]!=1){
			traceArr(roots[i], rootLen[i], arr_marktable[i], local);
		}
		else{
			void *child=CLEAR_PTR(roots[i][0]);
			if(child!=NULL)
				local->markstack[++(*pcounter)]=child;
		}
	}
}

//--------------------------------------------------End handle global roots.



static inline void resetSweepCounter(struct localAlloc *local){
	struct allocator *alc = local->global;
	unsigned long sweep_ctr, lsweep_ctr;
	do{
		sweep_ctr=alc->sweep_counter;
		if(PHASE(sweep_ctr)>=local->localVer) return;
		assert(PHASE(sweep_ctr)==local->localVer-2);
		lsweep_ctr = MAKE_SC(0, local->localVer);
	}while(!CAS(&alc->sweep_counter, sweep_ctr, lsweep_ctr));

}
static int CheckFinish(struct localAlloc *local, void **markstack, int *counterPtr){
	//Check Finish:
	struct allocator *alc=local->global;
	const int T = alc->numThreads;
	const long localVer = local->localVer;
	int curPhases[T];
	void *tracing[T];
	int counter=0;
CheckFinish:
	usleep(10);
	for(int i=0; i<T; ++i){
		struct localAlloc *ai = &alc->locals[i];
		curPhases[i]=ai->localVer;
		void *trptr = ai->markstack[0];
		tracing[i]=trptr;
		if(((long)trptr & 1) || curPhases[i]!=localVer)
			continue;
		void *ptr;
		for(int j=1; (ptr=ai->markstack[j])!=NULL; ++j){
			if(is_marked(ptr)==0 && counter<10){ //an unmarked pointer.
				markstack[++counter]=ptr;
				//goto CheckFinish;
			}
		}
		if(is_marked(trptr)==0)
			markstack[++counter]=trptr;
	}
	if(counter>=1){
		usleep(2);
		*counterPtr=counter;
		if(localVer!=alc->phase.phase){//is it relevant to help?
			for(int i=1; markstack[i]!=NULL; ++i)
				markstack[i]=NULL;
			return 1;//Not relevant - finish
		}
		return 0;//Still in current phase - help (and do not finish)
	}
	for(int i=0; i<T; ++i){
		struct localAlloc *ai = &alc->locals[i];
		void *trptr = ai->markstack[0];
		if(trptr!=tracing[i] || ai->localVer!=curPhases[i]) goto CheckFinish;
	}
	return 1;
}
static inline void *getChild(void *obj, int offset){
	void **ptr = (void*)(((char*)obj) + offset);
	return CLEAR_PTR(*ptr);
}
#if NCHILDS == 1
static inline void traceObj(void *cur, void **markstack, int *pcounter,
		unsigned long localSweep){
	mtype *h_ptr, mask, old;
	get_mark_params(cur, &h_ptr, &mask);
	old = *h_ptr;//read mark-bit word
	if(old&mask) //if is marked
		return;	//no need to trace

	markstack[0]=cur; //publish

	void *nxt = getChild(cur, CHILD1_OFFSET);
	if(nxt==NULL) {
		__mark(h_ptr, mask, old, localSweep);
	}
	else{
		markstack[*pcounter+1]=nxt;//By TSO this write comes after markstack[0]=cur;
		if(__mark(h_ptr, mask, old, localSweep)){
			(*pcounter)++;//commit previous write
		}
		else
			markstack[(*pcounter)+1]=NULL;
	}
}
#else
static inline void traceObj(void *cur, void **markstack, int *pcounter,
		unsigned long localSweep){
	if(is_marked(cur, localSweep))
		return;
	markstack[0]=cur;
	int cctr=0;
	void *c = getChild(cur, CHILD1_OFFSET);
	if(c!=NULL)
		markstack[*pcounter+(++cctr)]=c;//put child, tentatively increase counter.
	c = getChild(cur, CHILD2_OFFSET);
	if(c!=NULL)
		markstack[*pcounter+(++cctr)]=c;//put child, tentatively increase counter.
#if NCHILDS > 2
	c = getChild(cur, CHILD3_OFFSET);
	if(c!=NULL)
		markstack[*pcounter+(++cctr)]=c;//put child, tentatively increase counter.
#endif
	if(mark(cur, localSweep))
		*pcounter += cctr;//commit increase counter.
	else
		for(int i=1; i<=cctr; ++i)//abort increase counter.
			markstack[*pcounter+i]=NULL;//remove written fields.
}
#endif
static void traceMarkstack(void **markstack, int counter, const unsigned long * const globalSweepPtr,
		unsigned long localSweep, struct localAlloc *local){
	while(counter>=1){
		Entry *cur=markstack[counter--];
		_assert(cur!=NULL && assert_in_heap(cur));
		traceObj(cur, markstack, &counter, localSweep);
	}
}
static inline void copyPartOfArr(void **markstack, void **arr, int begin, int *pcounter){
	*pcounter=0;
	for(int k=0; k<ARR_ENTRIES_PER_BIT; ++k){
		void *ptr=arr[begin+k];
		if(ptr){
			markstack[++(*pcounter)]=ptr;
		}
	}

}
static int get_arr_chunk(u64 *syn_ctr, int len, u64 localSweep){
	u64 ctr=*syn_ctr;
	//optimistically check whether the task was already finished before changing the counter.
	if((unsigned)ctr*ARR_ENTRIES_PER_BIT>=len) return -1;
	if(!is_cur_phase(ctr, localSweep)) return -1; //not the right phase.
	ctr=__sync_fetch_and_add(syn_ctr, 1);
	if((unsigned)ctr*ARR_ENTRIES_PER_BIT>=len) return -1;
	if(!is_cur_phase(ctr, localSweep)) return -1; //not the right phase.
	return (int)(ctr&0x7FFFFFFF);
}
//mark an array bit as finished.This signify that the array chunk is already
//copied to the markstack. If successfully mark the chunk continue with marking children.
static int sign_bit(u64 *bits_ptr, int bit, u64 localSweep){
	u64 mask = 1ull<<bit, old;
	do{
		old = *bits_ptr;
		if(!is_cur_phase(old, localSweep)) return 0;//false
		if(old&mask) return 0;
	}while(!CAS(bits_ptr, old, old|mask));
	return 1;
}
static void traceArr(void **arr, const int len, void *base, struct localAlloc *local){
	const unsigned long localSweep=MAKE_SC(0, local->localVer);
	const unsigned long *const globalSweepPtr=&local->global->sweep_counter;
	void **markstack=local->markstack;
	u64 *syn_ctr=(u64 *)base;//atomic counter to optimistically synchronize effort.
	u64 *bits=syn_ctr+1;//markbits to fine tune synchronized effort.
	int ctr=0;

	while(1){
		ctr = get_arr_chunk(syn_ctr, len, localSweep);
		if(ctr==-1) break;

		int counter=0;
		copyPartOfArr(markstack, arr, ctr*ARR_ENTRIES_PER_BIT, &counter);
		markstack[0]=markstack[counter];
		if(sign_bit(bits+(ctr/32), ctr%32, localSweep))
			traceMarkstack(markstack, counter, globalSweepPtr, localSweep, local);
		markstack[0]=(void*)((long)markstack[0]|1);//not processing anything right now.
	}
	finishTraceArr(local, arr, len, bits, markstack, localSweep, globalSweepPtr);
}
//help to finish moving the array into markstacks.
static void finishTraceArr(struct localAlloc *local, void ** arr, int len,u64* bits,
		void **markstack,u64 localSweep,	const unsigned long *const globalSweepPtr){
	int num_markwords = (len/ARR_ENTRIES_PER_BIT)/32;
	for(int i=0; i<num_markwords; ++i){
		u64 val = bits[i];
		if( !is_cur_phase(val, localSweep)) break;
		unsigned uval=(unsigned)val;
		if(uval==0xFFFFFFFF) continue;//work finished for this qword
		for(int j=0; j<32; ++j){
			if((uval&(1<<j))==0){
				int counter=0;
				copyPartOfArr(markstack, arr, (i*32+j)*ARR_ENTRIES_PER_BIT, &counter);
				markstack[0]=markstack[counter];
				if(sign_bit(bits+i, j, localSweep))
					traceMarkstack(markstack, counter, globalSweepPtr, localSweep, local);
				markstack[0]=(void*)((long)markstack[0]|1);//not processing anything right now.
			}
		}
	}
}

static void trace(void *HPs[], int len, struct localAlloc *local){
	int counter=0;
	const unsigned long localSweep=MAKE_SC(0, local->localVer);
	const unsigned long *const globalSweepPtr=&local->global->sweep_counter;
	void **markstack=local->markstack;
	traceRoots(local, &counter);

	for(int i=0; i<len; ++i){
		assert(HPs[i]!=NULL && CLEAR_PTR(HPs[i])==HPs[i]);
		markstack[++counter]=HPs[i];
	}
	traceMarkstack(markstack, counter, globalSweepPtr, localSweep, local);
	markstack[0]=(void*)((long)markstack[0]|1);//I finished.
	while(!CheckFinish(local, markstack, &counter)){
		traceMarkstack(markstack, counter, globalSweepPtr, localSweep, local);
		markstack[0]=(void*)((long)markstack[0]|1);//I finished.
	}
	//move sweep counter to current phase
	resetSweepCounter(local);
}

//TODO: optimize. perf reveals that verCAS takes more that 1% of the total exec time for hash 64 threads.
static int pReturnCache(struct localAlloc *local){
	struct allocator *alc = local->global;
	struct allocEntry *toalloc=local->alloc_cache;
	struct verEntry old, newitem;
	do{ //push toalloc to head.
		old = alc->head;
		if(old.ver != local->localVer){
			local->alloc_cache->free=1;//discard allocation cache. Just let the loop continue until it finishes.
			return 0;
		}
		toalloc->next = old.head;
		newitem.head=toalloc;
		newitem.ver=local->localVer;
	}while(!verCAS(&alc->head, old, newitem));
	local->alloc_cache=NULL;
	return 1;
}
static void setAsFree(struct localAlloc *local, void *obj){
	if(local->alloc_cache!=NULL){
		if(local->alloc_cache->free < ENTRIES_PER_CACHE){
			local->alloc_cache->ptrs[local->alloc_cache->free++] = obj;
			PRINTF2("pReturnAlloc %p. cache=%p[%d]\n", obj,local->alloc_cache,
					local->alloc_cache->free);
			return;
		}
		else{
			pReturnCache(local);
		}
	}
	struct allocator *alc = local->global;

	local->alloc_cache = getAllocEntry();
	local->alloc_cache->size = alc->entrySize;
	local->alloc_cache->free = 1;
	local->alloc_cache->ptrs[0]=obj;
}

static int process_sweep_page(unsigned page_number, struct localAlloc *local){
	char *page = (char*)NAME(heapmem)[page_number/SWEEP_PAGES_PER_CHUNK]+(page_number%SWEEP_PAGES_PER_CHUNK)*SWEEP_PAGE;
	void *base = (void*)((long)page & HEADER_MASK);
	int offset = (long)page & OFFSET_MASK;
	offset/=MEM_PER_BYTE;
	unsigned long *h_ptr = (unsigned long*)((unsigned char*)base + offset);
	assert(((long)h_ptr&7) == 0);
	const int OBJ_SIZE = local->global->entrySize;
	assert(OBJ_SIZE==NODE_SIZE);
	assert((OBJ_SIZE&(OBJ_SIZE-1))==0);//obj_size must be a power of 2.

	char *obj = page;
	if(page==(char*)base){//MUST not sweep the header!
		unsigned u64ignores=HEADER_SIZE/(MEM_PER_BYTE*sizeof(unsigned long));
		//assert(u64ignores!=0);//HEADER SIZE must be bigger than MEM_PER_BYTE*sizeof(unsigned long)
		h_ptr+=u64ignores;
		if(u64ignores)
			obj+=HEADER_SIZE;
	}
	while(obj<page+SWEEP_PAGE){
		unsigned long mark_bits = *h_ptr;
		for(unsigned int mask=1; mask!=0u/*1<<32*/; mask<<=1){
			if(!(mark_bits&mask)){
				DALLOW_SMALL_SP(if(((long)obj&OFFSET_MASK)>=HEADER_SIZE))
				setAsFree(local, obj);
			}
			obj += OBJ_SIZE;
		}
		h_ptr++;
	}
	return 0;
}
static void sweep(struct localAlloc *local){
	local->alloc_cache=NULL;//contains candidates for current phase.
	struct allocator *alc = local->global;
	const int num_sweep_pages = NAME(heapmemcnt)*SWEEP_PAGES_PER_CHUNK;
	unsigned phase = local->localVer;
	unsigned long lsweep, newlsweep;
	int ctr=0;
	while(1){
		do{//take a single sweep page.
			lsweep=alc->sweep_counter;
			if(PHASE(lsweep)!=phase || SCOUNTER(lsweep)>=num_sweep_pages){
				if(local->alloc_cache!=NULL)
					pReturnCache(local);
				local->alloc_cache=NULL;
				return; //a new phase already started or sweep finished.
			}
			newlsweep=lsweep+1;
		}while(!CAS(&alc->sweep_counter, lsweep, newlsweep));
		//if some thread is in middle of clearing marks, do not sweep that page.
		ctr += process_sweep_page(SCOUNTER(lsweep), local);
	}
}
static void clearArrMarks(struct localAlloc *local, unsigned long phaseNum, void *markbits, int len){
	phaseNum<<=32;//contains a clear mark: phase number in high 32 bits, 0 in lower 32 bits.
	unsigned long *s=(unsigned long*)markbits, *e=(unsigned long*)((char*)markbits+len);
	for(unsigned long *h=s, old; h<e; h++){
		do{
			old=*h;
			if(old>=phaseNum) break;
		}while(!CAS(h, old, phaseNum));
	}
	/* An alternative option - do not improve performance. 
	unsigned long *h=s+local->tid/4, *he=h, old;
	do{
		do{
			old=*h;
			if(old>=phaseNum) break;
		}while(!CAS(h, old, phaseNum));
		h++;
		if(h==e) h=s;
	}while(h!=he);*/
}
static void clearMarks(struct localAlloc *local, unsigned long phaseNum){
	phaseNum<<=32;//contains a clear mark: phase number in high 32 bits, 0 in lower 32 bits.
	const unsigned long tot_quota=HEADER_SIZE/sizeof(unsigned long);
	const unsigned long quota = tot_quota / local->global->numThreads;
	const int tid = local->tid;

	for(int i=0; i<NAME(heapmemcnt); ++i){//for each chunk
		char *curheap = NAME(heapmem)[i];
		unsigned long *s=(unsigned long*)curheap, *e=(unsigned long*)(curheap+HEADER_SIZE);
		unsigned long *first=s+ tid*quota;
		unsigned long *h=first;
		do{
			unsigned long old;
			do{//*h=phaseNum;
				old=*h;
				if(old>=phaseNum) break;
			}while(!CAS(h, old, phaseNum));
			h=(h+1<e)?h+1:s;//if wrapping start from beginning..
			if(h==first) break;//finished.
		}while(1);
	}
}

/*static inline int test_and_set_bit(int nr, volatile unsigned long *addr)
{
	int oldbit;
	__asm__ volatile("lock;" "bts %2,%1\n\t"
			"sbb %0,%0" : "=r" (oldbit), "+m" (*(volatile long *) (addr)) : "Ir" (nr) : "memory");
	return oldbit;
}
int mark2(void *ptr){
  ON AMD, nr to test_and_set_bit must be 0<=nr<32
        void *base = (void*)((long)ptr & HEADER_MASK);
        int offset = (long)ptr & OFFSET_MASK;
        offset/=MEM_PER_BIT;
  return test_and_set_bit(offset, (volatile unsigned long*)base);
}*/

