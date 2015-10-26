#ifndef _LOCKFREE_LIST_H_FILE__
#define _LOCKFREE_LIST_H_FILE__

#include "globals.h"
#include "Atomic.h"
#include "entry.h"


Entry* entryHead;				// list
struct DirtyRecord *threadsDirties;
extern volatile Bool run, stop;				// Used to make the threads to busy wait till Stage I is done
typedef struct{
	Entry **prev, *cur, *next;
}ThreadLocal;

Bool ListSearch(Entry** entryHead, ThreadGlobals* tg, int key, Data *data);
Bool ListInsert(Entry** entryHead, ThreadGlobals* tg, int key, Data data);
Bool ListDelete(Entry** entryHead, ThreadGlobals* tg, int key);


#endif //_LOCKFREE_LIST_H_FILE__
