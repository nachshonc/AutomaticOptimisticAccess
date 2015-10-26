#include "debugging.h"
#include "lfhash.h"
#include "lfl.h"
#include <unistd.h>
#include <string.h>
#include "allocator.h"
const int CLOG_ENTRIES=50;

struct {	void *n, *pr; int tid, ln; char f[10];} arr[S];
unsigned long h;
int lk=0;
extern void *heapmem[MAX_CHUNKS];
extern int heapmemcnt;

void cLog(void *n, void *pr, int tid, int line, const char *name){
	if(lk){
		sleep(1);
		assert(!"lock is set");
	}
	int idx=__sync_fetch_and_add(&h, 1)%S;
	arr[idx].tid=tid;
	arr[idx].ln=line;
	arr[idx].n=n;
	arr[idx].pr=pr;
	memset(arr[idx].f, '*', sizeof(arr[idx].f));
	if(strlen(name)>=sizeof(arr[idx].f)){
		strncpy(arr[idx].f, name, sizeof(arr[idx].f));
		arr[idx].f[sizeof(arr[idx].f)-1]='\0';
	}
	else
		strcpy(arr[idx].f, name);
	//arr[idx].f=name;
}
void slog(Entry* v){//should be called by gdb..
	int pos=-1, i;
	for(i = (int)(h%S)-CLOG_ENTRIES; i<h%S; ++i){
		if((i%3)==1) printf("[%3d]: ", i);
		printf("%10s:%3d[%3d] %8p %8p %c",
				arr[i].f,arr[i].ln,arr[i].tid, arr[i].n, arr[i].pr,
				(i%3)?';':'\n');
		if(arr[i].n==v || arr[i].pr==v)
			pos=i;
	}
	if(pos==-1)
		printf("\n%p not found\n", v);
	else
		printf("\n%p last found at index %d\n", v, pos);
}
typedef unsigned long mtype;
static inline int is_marked(void *ptr){
	void *base = (void*)((long)ptr & (~(CHUNK_SIZE-1)));
	int offset = (long)ptr & (CHUNK_SIZE-1);
	offset/=NODE_SIZE;
	mtype *h_ptr = (mtype*)base + offset/(sizeof(mtype)*8/2);
	mtype mask= (mtype)1<<(offset&(sizeof(mtype)*8/2-1));
	return (*h_ptr)&mask;
}

void barrier(int tid, int numT);
void B(void *p){
	printf("assertion failed (stop=%d)\n", stop);
	if(!stop){
		for(int i=0; i<1000000000; ++i) __asm__ volatile ("nop");
		assert(!"not all were marked");
	}
}
void clearDebugging(){
	for(int i=0; i<(int)(h%S)-CLOG_ENTRIES; ++i)
		memset(&arr[i], 0, sizeof(arr[i]));//{arr[i].ln=0; arr[i].tid=0; arr[i].n=0; arr[i].pr=0;}
	for(int i=h%S; i<S; ++i)
		memset(&arr[i], 0, sizeof(arr[i]));//{arr[i].ln=0; arr[i].tid=0; arr[i].n=0; arr[i].pr=0;}
}
void verifyList(Entry *e){
	Entry *volatile prev=NULL;
	while(e!=NULL){
		if(!is_marked(e)){
			clearDebugging();
			B(prev);
		}
		prev=e;
		e=clearDeleted(e->nextEntry);
	}
}
void verify(struct localAlloc *local){
	barrier(local->tid, local->global->numThreads);
	struct allocator *global=local->global;
	verifyList(entryHead);
	for(int i=0;  i < global->numThreads ;  ++i) {
		Entry* hptr0 = threadsDirties[i].HPs[0];
		Entry* hptr1 = threadsDirties[i].HPs[1];
		Entry* hptr2 = threadsDirties[i].HPs[2];
		verifyList(hptr0);
		verifyList(hptr1);
		verifyList(hptr2);
	}
	barrier(local->tid, local->global->numThreads);
}

void verifyH(struct localAlloc *local){
	barrier(local->tid, local->global->numThreads);
	struct allocator *global=local->global;
	for(int i=0; i<hash.len; ++i)
		verifyList(hash.array[i]);
	for(int i=0;  i < global->numThreads ;  ++i) {
		Entry* hptr0 = threadsDirties[i].HPs[0];
		Entry* hptr1 = threadsDirties[i].HPs[1];
		Entry* hptr2 = threadsDirties[i].HPs[2];
		verifyList(hptr0);
		verifyList(hptr1);
		verifyList(hptr2);
	}
	barrier(local->tid, local->global->numThreads);
}

int NumberOfSetBits(unsigned i)
{
	i = i - ((i >> 1) & 0x55555555);
	i = (i & 0x33333333) + ((i >> 2) & 0x33333333);
	return (((i + (i >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
}
#ifdef MAOA
void dumpMarks(){
	for(int i=0; i<heapmemcnt; ++i){
		char *curheap = heapmem[i];
		printf("chunk %p: ", curheap);
		int ctr=0;
		for(int i=0; i<HEADER_SIZE; i+=4){
			unsigned res = *(unsigned*)(curheap+i);
			printf("%08x ", res);
			ctr+=NumberOfSetBits(res);
		}
		printf(". (%d)\n", ctr);
	}
}
#endif

void bubbleSortLong(void** array, int size)
{
	int swapped;
	int i;
	for (i = 1; i < size; i++)
	{
		swapped = 0;    //this flag is to check if the array is already sorted
		int j;
		for(j = 0; j < size - i; j++)
		{
			if((char*)array[j] > (char*)array[j+1])
			{
				void *temp = array[j];
				array[j] = array[j+1];
				array[j+1] = temp;
				swapped = 1;
			}
		}
		if(!swapped){
			break; //if it is sorted then stop
		}
	}
}
int unique(void** a, int n)   //sorted array
{
	int k = 0;
	for (int i = 1; i < n; i++) {
		if (a[k] != a[i]) {              //comparing the first 2 unequal numbers
			a[k+1] = a[i];                 //shifting to the left if distinct
			k++;
		}
	}
	return (k+1);             //return the last index of new array i.e. the number of distinct elements
}
