#define _GNU_SOURCE
#include "lfl.h"
#include "globals.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stddef.h>
#include "allocator.h"
#define NS AOA
#define NCHILDS 1
#define CHILD1_OFFSET offsetof(Entry, nextEntry)
#include "allocator.c"
#include "debugging.h"

#define HELP_DELETE_ON_FIND
#define HPCAS
#ifndef TYPE_SAFE
#define CHECKREADS
#endif
//#define COUNTLEN(S) S
#define COUNTLEN(S)

#define CHECK(label) do{__asm__ volatile ("" ::: "memory");\
		if(unlikely(dty->dirty)) \
		{dty->dirty=0; helpCollection(tg->entryAllocator); dty->dirty=0; goto label;}} while(0)
//#define FENCE_CHECK(label) if(unlikely(CAS(&dty->dirty, 1, 0))) goto label;
#define FENCE_CHECK(label) do{__asm__ volatile ("" ::: "memory");\
		if(unlikely(__sync_lock_test_and_set(&dty->dirty, 0))) \
		{dty->HPs[0]=dty->HPs[1]=dty->HPs[2]=NULL; helpCollection(tg->entryAllocator); \
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
//--------------
/*	struct DirtyRecord * const dty=tg->dirty;
start: (void)0;
	Entry *cur = *entryHead; CHECK(start);
	int ismarked = isDeleted(cur);
	cur = clearDeleted(cur);
	while(cur!=NULL){
		uint64_t kd = cur->keyData;
		cur = cur->nextEntry;
		CHECK(start);
		if(getKey(kd)>=key){
			if(getKey(kd)==key && !ismarked)
			{
				*data = getData(kd);
				return TRUE;
			}
			return FALSE;
		}
		ismarked = isDeleted(cur);
		cur = clearDeleted(cur);
	}
	return FALSE;*/
//--------------
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
		Entry *newEntry = (Entry*)pallocAOA(tg->entryAllocator);
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
		//pReturnAlloc(tg->entryAllocator, newEntry);
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
void *palloc(struct localAlloc *local){return pallocAOA(local);}
void addRoot(void **root, int len){addGlobalRootAOA(root, len);}
