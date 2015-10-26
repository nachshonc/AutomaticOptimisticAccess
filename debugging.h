#include <assert.h>
#include <unistd.h>
#define DEBUGGING_ASSERTIONS
#define S 100
extern int lk;
void cLog(void *n, void *pr, int tid, int line, const char *name);

#ifdef _assert
#undef _assert
#endif
#ifdef DEBUGGING_ASSERTIONS
#define _assert(B) do{if(lk){sleep(1); assert(0);} if(!(B)){lk=1; __sync_synchronize(); sleep(1); assert(0);}}while(0)
#else
#define _assert(B)
#endif

#define SET(n, pr) cLog(n, pr, local->tid, __LINE__, __FUNCTION__)
#define SETDS(n, pr) cLog(n, pr, tg->input.threadID, __LINE__, __FUNCTION__)

struct localAlloc;
void verify(struct localAlloc *local);
void verifyH(struct localAlloc *local);
void barrier(int, int);
