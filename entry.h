#ifndef __ENTRY_H_
#define __ENTRY_H_

#include <stdint.h>

#define DELETED_FLAG			0x0000000000000001
#define HIGH_32_MASK			0xFFFFFFFF00000000
#define LOW_32_MASK				0x00000000FFFFFFFF


/*==============================   GLOBAL TYPES   ==============================*/
enum BOOLEAN {FALSE, TRUE};
typedef enum BOOLEAN Bool;
/*=================================   ENTRY'S TYPE & STRUCTURE  =================================*/
typedef int Data;

typedef struct Entry_t {
	long 		refcount;		// timestamp saying when this node was inserted
	uint64_t		keyData;
	struct Entry_t* nextEntry;
	uint64_t		retireTS;		// timestamp saying when node was retired
} Entry;
/*==============================   INLINE FUNCTIONS IMPLEMENTATIONS  ==============================*/
static inline Bool isDeleted(Entry* p) {
    if ( ((int64_t)p & DELETED_FLAG) ) {
        return TRUE;
    }
    return FALSE;
}
static inline Entry* markDeleted(Entry* p) {
    return (Entry*)( (int64_t)p | DELETED_FLAG );
}
static inline Entry* clearDeleted(Entry* p) {
    return (Entry*)( (int64_t)p & ~DELETED_FLAG );
}
/*****************************************************************************
	entPtr :
	returns start of Entry by the nextEntry word (address manipulation)
	needed for getting pointer to the previous entry from the tg->prev pointer
*****************************************************************************/
static inline Entry* entPtr(Entry** word) {
	return (Entry*) ((char*)(word) - (unsigned long)(&((Entry*)0)->nextEntry));
}

static inline Data getData(uint64_t keydata){return (Data)(keydata>>32);}
static inline int getKey(uint64_t keydata) {
	return (int)keydata;
}

#endif // __ENTRY_H_
