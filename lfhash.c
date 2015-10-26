#include "lfl.h"
#include "lfhash.h"
#include "globals.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "debugging.h"
#include "allocator.h"

void initHash(HashMap *hash, int logLen){
	int len = 1<<logLen;
	hash->array = malloc(sizeof(Entry*)*len);
	_assert(hash->array!=NULL);
	hash->logLen=logLen;
	hash->len = len;
	memset(hash->array, 0, sizeof(Entry*)*len);
#ifdef DAOA
	void addRoot(void **root, int len);
	addRoot((void **)hash->array, len);
#endif
}

/*****************************************************************************/
Bool HashSearch(HashMap *hash, ThreadGlobals* tg, int key, Data* data) {
	int bucket = HASH(key, hash->logLen);
	_assert(bucket>=0 && bucket<hash->len);
	return ListSearch(&hash->array[bucket], tg, key, data);
}
/*****************************************************************************/
Bool HashInsert(HashMap *hash, ThreadGlobals* tg, int key, Data data) {
	int bucket = HASH(key, hash->logLen);
	_assert(bucket>=0 && bucket<hash->len);
	return ListInsert(&hash->array[bucket], tg, key, data);
}
/*****************************************************************************/
Bool HashDelete(HashMap *hash, ThreadGlobals* tg, int key) {
	int bucket = HASH(key, hash->logLen);
	_assert(bucket>=0 && bucket<hash->len);
	return ListDelete(&hash->array[bucket], tg, key);
}

/*****************************************************************************/
//For debugging
int Count(HashMap *hash){
	int cnt=0;
	for(int i=0; i<hash->len; ++i)
	{
		Entry *head = hash->array[i];
		while(head!=NULL){
			cnt++;
			head=clearDeleted( head->nextEntry);
		}
	}
	return cnt;
}
