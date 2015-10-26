#ifndef _GLOBALS_H_FILE_
#define _GLOBALS_H_FILE_

#include "Atomic.h"

#define unlikely(EXP) __builtin_expect(EXP, 0)
#define likely(B) __builtin_expect(!!(B), 1)
#define CFENCE __asm__ volatile ("":::"memory")
#define __compiler_fence() CFENCE

#define DEB(...)
//#define DEB(...) __VA_ARGS__
//#define DEBUG
//#define DEBUG2
//#define DEBUGR
//#define DEBUG4

// PRINTF to be used for debugging instead of printf, in order to shorten the code
#ifdef DEBUG
#define PRINTF printf
#else
#define PRINTF(...) ((void)0)
#endif

#ifdef DEBUG2
#define PRINTF2 printf
#else
#define PRINTF2(...) ((void)0)
#endif


#ifdef DEBUGR           // recovery-related printouts
#define PRINTFR printf
#else
#define PRINTFR(...) ((void)0)
#endif




typedef struct Input_t {

    int			threadNum;			// Total number of threads
    int			threadID;			// The thread ID of this thread
    float		fractionDeletes;
    float		fractionInserts;
    int			elementsRange;

} Input;

struct DirtyRecord;
struct localAlloc;

typedef struct ThreadGlobals_t {	
	struct DirtyRecord *dirty;
	struct localAlloc *entryAllocator;
	Input input;				// Input-output information of the thread
}__attribute__((aligned(64)))  ThreadGlobals;

#endif	//_GLOBALS_H_FILE_
