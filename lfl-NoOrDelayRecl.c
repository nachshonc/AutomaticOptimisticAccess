#include "lfl.h"
#include "globals.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
//#include <glib.h>
#include "allocator.h"
#include "debugging.h"


#ifndef MALLOC
#define ALLOC palloc(tg->entryAllocator)
#define POOL(A) A
#else
#define ALLOC malloc(sizeof(Entry))
#define POOL(A)
#endif











static inline Bool find(Entry** entryHead, ThreadGlobals* tg, int key, ThreadLocal *fres);
/*****************************************************************************/
Bool ListSearch(Entry** entryHead, ThreadGlobals* tg, int key, Data* data) {
	ThreadLocal fres;



	if (find(entryHead, tg,key, &fres)) {
		*data=getData(fres.cur->keyData);
		return TRUE;
	}
	return FALSE;
//--------------
/*	Entry *cur = *entryHead;
	int ismarked = isDeleted(cur);
	cur = clearDeleted(cur);
	while(cur!=NULL){
		uint64_t kd = cur->keyData;
		if(getKey(kd)>=key){
			if(getKey(kd)==key && !ismarked)
			{
				*data = getData(kd);
				return TRUE;
			}
			return FALSE;
		}
		cur = cur->nextEntry;
		ismarked = isDeleted(cur);
		cur = clearDeleted(cur);
	}
	return FALSE;*/
//--------------
}
/*****************************************************************************/
Bool ListInsert(Entry** entryHead, ThreadGlobals* tg, int key, Data data) {

	ThreadLocal fres;

	while (TRUE) {
		if (find(entryHead, tg,key, &fres)) {
			return FALSE; //key exists
		}

		//create entry
		Entry *newEntry = (Entry*)ALLOC;




		newEntry->keyData = key;
		newEntry->nextEntry = fres.cur;

		//connect
		if (CAS(fres.prev, fres.cur, newEntry)) {

			return TRUE;
		}
		//POOL(pReturnAlloc(tg->entryAllocator, newEntry));

	}
}
/*****************************************************************************/
Bool ListDelete(Entry** entryHead, ThreadGlobals* tg, int key) {

	ThreadLocal fres;

	while (TRUE) {
		if (find(entryHead, tg, key, &fres) == FALSE)	{
			return FALSE; //key not found
		}





		if ( CAS(&(fres.cur->nextEntry), fres.next, markDeleted(fres.next) ) == FALSE) {

			continue; //try until delete succefull
		}
#ifdef HASH_OP
		find(entryHead, tg, key, &fres); //force list update
#endif

		return TRUE;
	}
}
/*****************************************************************************/
static inline Bool find(Entry** entryHead, ThreadGlobals* tg, int key, ThreadLocal *fres) {
	int ckey;
try_again:
	fres->prev = entryHead;
	fres->cur = *(fres->prev);
	while (fres->cur != NULL) {
		fres->next = fres->cur->nextEntry;




		if(isDeleted(fres->next)) {




			if (CAS(fres->prev, fres->cur, clearDeleted(fres->next)) == FALSE) {

				goto try_again; //connect failed try again
			}


			fres->cur = clearDeleted(fres->next);
		}
		else {

			ckey = getKey(fres->cur->keyData);





			if (*(fres->prev) != fres->cur) {
				goto try_again;
			}

			if (ckey >= key) {

				return (ckey == key); //compare search key
			}

			fres->prev = &(fres->cur->nextEntry);
			fres->cur = fres->next;
		}
	} //end of while
	return FALSE;
}
