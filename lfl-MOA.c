#include "lfl.h"
#include "globals.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "allocator.h"
#include "debugging.h"
void pReturnAlloc(struct localAlloc *local, void *obj);

#define HELP_DELETE_ON_FIND
#define HPCAS
#ifndef TYPE_SAFE
#define CHECKREADS
#endif
//#define COUNTLEN(S) S
#define COUNTLEN(S)

#define CHECK(label) do{__asm__ volatile ("" ::: "memory");\
		if(unlikely(dty->dirty)) \
		{dty->dirty=0; goto label;}} while(0)
//#define FENCE_CHECK(label) if(unlikely(CAS(&dty->dirty, 1, 0))) goto label;
#define FENCE_CHECK(label) do{__asm__ volatile ("" ::: "memory");\
		if(unlikely(__sync_lock_test_and_set(&dty->dirty, 0))) \
		{dty->HPs[0]=dty->HPs[1]=dty->HPs[2]=NULL; \
		dty->dirty=0; goto label;}}while(0)

//if(__sync_lock_test_and_set(p, 0));


static inline Bool find(Entry** entryHead, ThreadGlobals* tg, int key, ThreadLocal *fres, struct DirtyRecord * const dty, uint64_t *keyData);
/*****************************************************************************/
Bool ListSearch(Entry** entryHead, ThreadGlobals* tg, int key, Data* data) {
	ThreadLocal fres;
	struct DirtyRecord * const dty=tg->dirty;
	uint64_t kd;
//start:
	if (find(entryHead, tg,key, &fres, dty, &kd)) {
		*data=kd;
		return TRUE;
	}
	return FALSE;
}
/*****************************************************************************/
Bool ListInsert(Entry** entryHead, ThreadGlobals* tg, int key, Data data) {
	struct DirtyRecord * const dty=tg->dirty;
	ThreadLocal fres;
start:
	while (TRUE) {
		if (find(entryHead, tg,key, &fres, dty, NULL)) {
			return FALSE; //key exists
		}

		//create entry
		Entry *newEntry = (Entry*)palloc(tg->entryAllocator);
		dty->HPs[0]= (fres.prev!=entryHead)?entPtr(fres.prev):NULL;
		dty->HPs[1]= fres.cur;
		dty->HPs[2]= newEntry;
		FENCE_CHECK(start);
		newEntry->keyData = key;
		newEntry->nextEntry = fres.cur;

		//connect
		if (CAS(fres.prev, fres.cur, newEntry)) {
			dty->HPs[0]=	dty->HPs[1]=	dty->HPs[2]=NULL;
			return TRUE;
		}
		pReturnAlloc(tg->entryAllocator, newEntry);
		dty->HPs[0]=dty->HPs[1]=dty->HPs[2]=NULL;
	}
}
/*****************************************************************************/
Bool ListDelete(Entry** entryHead, ThreadGlobals* tg, int key) {
	struct DirtyRecord * const dty=tg->dirty;
	ThreadLocal fres;
start:
	while (TRUE) {
		if (find(entryHead, tg, key, &fres, dty, NULL) == FALSE)	{
			return FALSE; //key not found
		}

		dty->HPs[0]=fres.cur;
		dty->HPs[1]=fres.next;
		FENCE_CHECK(start);
		//EXECUTOR
		if ( CAS(&(fres.cur->nextEntry), fres.next, markDeleted(fres.next) ) == FALSE) {
			dty->HPs[0]=dty->HPs[1]=dty->HPs[2]=NULL;
			continue; //try until delete successful
		}
#ifdef HASH_OP
		find(entryHead,tg, key, &fres, dty, NULL); //force list update
#endif
		dty->HPs[0]=	dty->HPs[1]=	dty->HPs[2]=NULL;
		return TRUE;
	}
}
/*****************************************************************************/
static inline Bool find(Entry** entryHead, ThreadGlobals* tg, int key, ThreadLocal *fres, struct DirtyRecord * const dty, uint64_t *keyData) {
	int ckey;
try_again:
	fres->prev = entryHead; //which is the address of the global entryHead
	fres->cur = *(fres->prev);
	while (fres->cur != NULL) {
		Entry *nxt = fres->cur->nextEntry;
#ifdef CHECKREADS //validate read.
		CHECK(try_again);
#endif
		fres->next=nxt;
		if(isDeleted(fres->next)) {
			dty->HPs[0] = (fres->prev!=entryHead)?entPtr(fres->prev):NULL;
			dty->HPs[1] = fres->cur;
			dty->HPs[2] = clearDeleted(fres->next);
			FENCE_CHECK(try_again);
			if (CAS(fres->prev, fres->cur, clearDeleted(fres->next)) == FALSE) {
				dty->HPs[0]=dty->HPs[1]=dty->HPs[2]=NULL;
				goto try_again; //connect failed try again
			}
			else {dty->HPs[0]=dty->HPs[1]=dty->HPs[2]=NULL;}
			pfree(tg->entryAllocator, fres->cur);
			fres->cur = clearDeleted(fres->next);
		}
		else {
			uint64_t kd = fres->cur->keyData;
			ckey = getKey(kd);

			Entry *revalidatePrev = *(fres->prev);
#ifdef CHECKREADS //validate read.
			CHECK(try_again);
#endif
			if (revalidatePrev != fres->cur) {
				goto try_again;
			}

			if (ckey >= key) {
				if(keyData) *keyData=kd;
				return (ckey == key); //compare search key
			}

			fres->prev = &(fres->cur->nextEntry);
			fres->cur = fres->next;
		}
	} //end of while
	return FALSE;
}
#ifdef DEBUGGING_
void assertLL(const char *const name, int line, int tid, void *PTR){
	Entry *cur=entryHead, *prev=NULL;
	int prevkey=-100000000;
	while(cur!=NULL){
		Entry *nxt=cur->nextEntry;
		if(!isDeleted(nxt)){
			int ky=getKey(cur->keyData);
			if(prevkey>ky){
				int tag = __sync_fetch_and_add(&tags, 1);
				printf("%d: I thread %d, discovered (prev>cur) at %s:%d. PTR=%p\n",tag, tid, name, line, PTR);
				sleep(1);
				assert(prevkey<ky);
			}
			prevkey=ky;
		}
		prev=cur;
		cur=clearDeleted(nxt);
		if(prev==cur){
			int tag = __sync_fetch_and_add(&tags, 1);
			printf("%d: I, thread %d, discovered (prev==cur) at %s:%d. PTR=%p\n",tag, tid, name, line, PTR);
			unsigned int sleep(unsigned int); sleep(1);
			assert(!"prev=cur");
		}
	}
}
#endif
