#include "lfl.h"
#include "globals.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
//#include <glib.h>
#include "allocator.h"
#include "debugging.h"
#include "refcount.h"


/*#ifndef MALLOC
#define ALLOC palloc(tg->entryAllocator)
#define POOL(A) A
#else
#define ALLOC malloc(sizeof(Entry))
#define POOL(A)
#endif*/
#define ALLOC newRC(tg->entryAllocator)










static inline Bool find(Entry** entryHead, ThreadGlobals* tg, int key, ThreadLocal *fres);
/*****************************************************************************/
Bool ListSearch(Entry** entryHead, ThreadGlobals* tg, int key, Data* data) {
	ThreadLocal fres={NULL, NULL, NULL};


	if (find(entryHead, tg,key, &fres)) {
		*data=getData(fres.cur->keyData);
		release(fres.cur); release(fres.next);
		if(fres.prev!=NULL&&fres.prev!=entryHead) release(entPtr(fres.prev));
		return TRUE;
	}
	release(fres.cur); release(fres.next);
	if(fres.prev!=NULL&&fres.prev!=entryHead) release(entPtr(fres.prev));
	return FALSE;
}
/*****************************************************************************/
Bool ListInsert(Entry** entryHead, ThreadGlobals* tg, int key, Data data) {

	ThreadLocal fres={NULL, NULL, NULL};

	while (TRUE) {
		if (find(entryHead, tg,key, &fres)) {
			release(fres.cur); release(fres.next);
			if(fres.prev!=NULL&&fres.prev!=entryHead) release(entPtr(fres.prev));
			return FALSE; //key exists
		}

		//create entry
		Entry *newEntry = (Entry*)ALLOC;




		newEntry->keyData = key;
		newEntry->nextEntry = fres.cur;
		safeRead(&fres.cur);//another link to fres.cur from newEntry

		//connect
		if (CAS(fres.prev, fres.cur, newEntry)) {
			release(fres.cur); //prev doesn't point to cur
			//release the local variable fres.
			release(fres.next);
			release(fres.cur); 
			if(fres.prev!=NULL&&fres.prev!=entryHead) release(entPtr(fres.prev));
			return TRUE;
		}
		release(newEntry);
		release(fres.next);
		release(fres.cur);
        	if(fres.prev!=NULL&&fres.prev!=entryHead) release(entPtr(fres.prev));
		//POOL(pReturnAlloc(tg->entryAllocator, newEntry));
	}
}
/*****************************************************************************/
Bool ListDelete(Entry** entryHead, ThreadGlobals* tg, int key) {

	ThreadLocal fres={NULL, NULL, NULL};

	while (TRUE) {
		if (find(entryHead, tg, key, &fres) == FALSE)	{
			release(fres.cur); release(fres.next);
			if(fres.prev!=NULL&&fres.prev!=entryHead) release(entPtr(fres.prev));
			return FALSE; //key not found
		}





		if ( CAS(&(fres.cur->nextEntry), fres.next, markDeleted(fres.next) ) == FALSE) {
			release(fres.cur); release(fres.next);
			if(fres.prev!=NULL&&fres.prev!=entryHead) release(entPtr(fres.prev));
			continue; //try until delete succefull
		}
		release(fres.cur); release(fres.next);
		if(fres.prev!=NULL&&fres.prev!=entryHead) release(entPtr(fres.prev));

#ifdef HASH_OP
		find(entryHead, tg, key, &fres); //force list update
		release(fres.cur); release(fres.next);
		if(fres.prev!=NULL&&fres.prev!=entryHead) release(entPtr(fres.prev));
#endif

		return TRUE;
	}
}
/*****************************************************************************/
static inline Bool find(Entry** entryHead, ThreadGlobals* tg, int key, ThreadLocal *fres) {
	int ckey;
try_again:
	fres->prev = entryHead;
	fres->cur = safeRead(fres->prev);
	fres->next=NULL;//after find returns we must release next
	while (fres->cur != NULL) {
		fres->next = safeRead(&fres->cur->nextEntry);
		if(isDeleted(fres->next)) {
			if (CAS(fres->prev, fres->cur, clearDeleted(fres->next)) == FALSE) {
				release(fres->cur); release(fres->next);//reset fres.
                		if(fres->prev!=NULL&&fres->prev!=entryHead) release(entPtr(fres->prev));
				goto try_again; //connect failed try again
			}
			release(fres->cur);//removed from data structure
			safeRead(&fres->next); //add a reference to next, since prev now points it (but we're going to remove ref of cur).
			//Don't know if it is simple to create a simpler code.
			release(fres->cur);//removed from fres
			fres->cur = clearDeleted(fres->next);
		}
		else {
			ckey = getKey(fres->cur->keyData);
			if (*(fres->prev) != fres->cur) {
				release(fres->cur); release(fres->next);
                		if(fres->prev!=NULL&&fres->prev!=entryHead) release(entPtr(fres->prev));
				goto try_again;
			}
			if (ckey >= key) {
				return (ckey == key); //compare search key
			}
			if(fres->prev!=entryHead) release(entPtr(fres->prev));
			fres->prev = &(fres->cur->nextEntry);
			fres->cur = fres->next;
		}
	} //end of while
	return FALSE;
}
