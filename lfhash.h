/*
 * lfhash.h
 *
 *  Created on: Jun 2, 2014
 *      Author: nachshonc
 */

#ifndef LFHASH_H_
#define LFHASH_H_
#include "lfl.h"
//#define HASH_OP
#ifdef HASH_OP
#define DHASHOP(A) A
#define DNHASHOP(A)
#else
#define DHASHOP(A)
#define DNHASHOP(A) A
#endif

#define LOAD_FACTOR 0.75
#define RANDNUM (2654435761u)
#define HASH(key, logS) (((unsigned)(key*RANDNUM))>>(32-(logS)))

typedef struct{
	Entry **array; //array of points (to the first item in the list, NULL if empty)
	int logLen, len;
}HashMap;

HashMap hash;

Bool HashSearch(HashMap *hash, ThreadGlobals* tg, int key, Data* data);
Bool HashInsert(HashMap *hash, ThreadGlobals* tg, int key, Data data);
Bool HashDelete(HashMap *hash, ThreadGlobals* tg, int key);
void initHash(HashMap *hash, int logLen);


#endif /* LFHASH_H_ */
