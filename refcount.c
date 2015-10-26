#include "entry.h"
#include <stdlib.h>
#include <assert.h>
#include "refcount.h"
#include "allocator.h"
#define CAS __sync_bool_compare_and_swap
__thread Entry *freeList;

static long DecrementAndTestAndSet(Entry *ptr){
	long old=0, new=0;
	do{
		old = ptr->refcount;
		new = old-2;
		if(new==0)
			new=1;
	}while(!CAS(&ptr->refcount, old, new));
	return (old-new)&1;
}
static void clearLowestBit(Entry *ptr){
	long old, new;
	do{
		old=ptr->refcount;
		new=old-1;
	}while(!CAS(&ptr->refcount, old, new));
}
static void reclaim(Entry *p){
	assert(freeList == clearDeleted(freeList));
	p->nextEntry=freeList;
	freeList=p;
	assert(freeList == clearDeleted(freeList));
}

Entry *newRC(struct localAlloc *local){
	assert(freeList == clearDeleted(freeList));
	if(freeList==NULL){
		Entry *p = (Entry*)palloc(local);
		p->refcount=2;
		return p;
	}
	Entry *p = freeList;
	assert(freeList == clearDeleted(freeList));
	freeList = p->nextEntry;
	assert(freeList == clearDeleted(freeList));
	safeRead(&p);
	clearLowestBit(p);
	return p;
}

Entry *safeRead(Entry **p){
	while(1){
		Entry *q = *p;
		if(clearDeleted(q) == NULL) return q;
		__sync_fetch_and_add(&clearDeleted(q)->refcount, 2L);
		if(q == *p) return q;
		else release(q);
	}
}

void release(Entry *p){
	p=clearDeleted(p);
	if(p==NULL)
		return;
	if(DecrementAndTestAndSet(p)==0)
		return;
	release(p->nextEntry);
	reclaim(p);
}

int cntfree(){
	int c=0;
	Entry *cur=freeList;
	while(cur!=NULL){
		c++;
		cur=cur->nextEntry;
	}
	return c;
}

