/*
 * simpleSet.c
 *
 * A very simple Set implementation.
 * The set is an array of constant size.
 * When an element is added it is appended to the current elements.
 * When setContains() is first called, the array is sorted, and then the element is found by binary search.
 *
 * This implementation is good when we first add all the elements and then perform all the searches.
 *
 *
 *      Author: Elad Gidron
 */

#include "simpleSet.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static inline int hash_fun(unsigned key, unsigned try, unsigned max) {
    int res= (key + try) % max;
    assert(res>=0 && res<max);
    return res;
}

struct hash_table {
    int max;
    int number_of_elements;
    int elements[];
};

static struct hash_table *create_hash(int size, int max){
	struct hash_table *h=malloc(sizeof(struct hash_table)+size*sizeof(int));
	h->max=size;
	h->number_of_elements=0;
	memset(h->elements, 0, sizeof(int)*size);
	return h;
}
static void destroy_hash(struct hash_table *hash_table){
	free(hash_table);
}
static void clean_hash(struct hash_table *hash_table){
	memset(hash_table->elements, 0, hash_table->max*sizeof(int));
	hash_table->number_of_elements=0;
}

static void hash_insert(struct hash_table *hash_table, int data) {
    int try, hash;
    assert(data!=0);
    if(hash_table->number_of_elements > hash_table->max/2) {
        assert(!"HASH TABLE IS FULL"); // FULL
    }
    for(try = 0; 1; try++) {
        hash = hash_fun(data, try, hash_table->max);
        if(hash_table->elements[hash] == 0) { // empty cell
            hash_table->elements[hash] = data;
            hash_table->number_of_elements++;
            return;
        }
        else if(hash_table->elements[hash]==data)
        		return;

    }
}

static int hash_retrieve(struct hash_table *hash_table, int key) {
    int try, hash;
    for(try = 0; 1; try++) {
        hash = hash_fun(key, try, hash_table->max);
        if(hash_table->elements[hash] == 0) {
            return 0; // Nothing found
        }
        if(hash_table->elements[hash] == key) {
            return 1;
        }
    }
    return 0;
}
Set setInit(int maxSize){return create_hash(maxSize,maxSize);}
void setAdd(Set set, int addr){hash_insert(set, addr);}
int setContains(Set set, int addr){return hash_retrieve(set, addr);}
void setReset(Set set){clean_hash(set);}
void setDestroy(Set set){destroy_hash(set);}
#if 0
int main(){
	Set p=setInit(100);
	for(int i=1; i<50; i+=1)
		setAdd(p, i*100);
	for(int i=1; i<50; i+=1)
		setAdd(p, i*100);
	for(int i=0; i<100; ++i){
		if(!(i%10)) printf("\n[%d]: ", i);
		printf("%d, ", p->elements[i]);
	}

	for(int i=1; i<=300; ++i){
		assert(setContains(p, i)==!(i%100));
	}
	for(int i=1; i<50; i+=1){
		assert(setContains(p, 100*i));
	}
	setReset(p);
	for(int i=2; i<100; i+=2)
		setAdd(p, i);
	for(int i=2; i<100; ++i){
		assert(setContains(p, i)!=i%2);
	}
	setDestroy(p);
}
#endif


#if 0
struct set_t {
	int last;		// index of the last object in the set
	int isSorted;	// flag to determine if the data array is sorted
	int maxSize;	// max set size
	int* data;		// the data array
};

//AUX
void bubbleSort(int* array, int size);
int binarySearch(int array[], int Size, int value);

// Returns a set with maximun size of maxSize.
Set setInit(int maxSize) {
	Set s = (Set)malloc(sizeof(SetT));
	assert(s != NULL);
	s->data = (int*)malloc(sizeof(int) * maxSize);
	assert(s->data != NULL);
	s->last = 0;
	s->isSorted = 1;
	s->maxSize = maxSize;
	return s;
}

// Adds value val to set.
void setAdd(Set set, int val) {
	assert(set->last < set->maxSize);
	set->isSorted = 0;
	set->data[set->last] = val;
	set->last++;
}

// Returns TRUE if val is in set, otherwise returns false.
int setContains(Set set, int val) {
	if (!set->isSorted) {
		bubbleSort(set->data,set->last);
	}
	return binarySearch(set->data,set->last,val);
}

// Resets the set.
void setReset(Set set) {
	set->isSorted = 1;
	set->last=0;
}

//Frees the Set's resources.
void setDestroy(Set set) {
	free(set->data);
	free(set);
}


/****** AUX functions ******/

void bubbleSort(int* array, int size)
{
   int swapped;
   int i;
   for (i = 1; i < size; i++)
   {
       swapped = 0;    //this flag is to check if the array is already sorted
       int j;
       for(j = 0; j < size - i; j++)
       {
           if(array[j] > array[j+1])
           {
               int temp = array[j];
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

int binarySearch(int array[], int Size, int value)
{
    int low = 0, high = Size - 1, midpoint = 0;
    while (low <= high)
    {
            midpoint = low + (high - low)/2;
            if (value == array[midpoint])
            {
                    return TRUE;
            }
            else if (value < array[midpoint])
                    high = midpoint - 1;
            else
                    low = midpoint + 1;
    }
    return FALSE;
}
#endif



#if 0
#include <limits.h>
#include <string.h>

struct entry_s {
	int key;
	struct entry_s *next;
};

typedef struct entry_s entry_t;

struct hashtable_s {
	int size;
	struct entry_s **table;
};

typedef struct hashtable_s hashtable_t;


/* Create a new hashtable. */
hashtable_t *ht_create( int size ) {

	hashtable_t *hashtable = NULL;
	int i;

	if( size < 1 ) return NULL;

	/* Allocate the table itself. */
	if( ( hashtable = malloc( sizeof( hashtable_t ) ) ) == NULL ) {
		return NULL;
	}

	/* Allocate pointers to the head nodes. */
	if( ( hashtable->table = malloc( sizeof( entry_t * ) * size ) ) == NULL ) {
		return NULL;
	}
	for( i = 0; i < size; i++ ) {
		hashtable->table[i] = NULL;
	}

	hashtable->size = size;

	return hashtable;
}

/* Hash a string for a particular hash table. */
int ht_hash( hashtable_t *hashtable, char *key ) {

	unsigned long int hashval;
	int i = 0;

	/* Convert our string to an integer */
	while( hashval < ULONG_MAX && i < strlen( key ) ) {
		hashval = hashval << 8;
		hashval += key[ i ];
		i++;
	}

	return hashval % hashtable->size;
}

/* Create a key-value pair. */
entry_t *ht_newpair( char *key, char *value ) {
	entry_t *newpair;

	if( ( newpair = malloc( sizeof( entry_t ) ) ) == NULL ) {
		return NULL;
	}

	if( ( newpair->key = strdup( key ) ) == NULL ) {
		return NULL;
	}

	if( ( newpair->value = strdup( value ) ) == NULL ) {
		return NULL;
	}

	newpair->next = NULL;

	return newpair;
}

/* Insert a key-value pair into a hash table. */
void ht_set( hashtable_t *hashtable, char *key, char *value ) {
	int bin = 0;
	entry_t *newpair = NULL;
	entry_t *next = NULL;
	entry_t *last = NULL;

	bin = ht_hash( hashtable, key );

	next = hashtable->table[ bin ];

	while( next != NULL && next->key != NULL && strcmp( key, next->key ) > 0 ) {
		last = next;
		next = next->next;
	}

	/* There's already a pair.  Let's replace that string. */
	if( next != NULL && next->key != NULL && strcmp( key, next->key ) == 0 ) {

		free( next->value );
		next->value = strdup( value );

	/* Nope, could't find it.  Time to grow a pair. */
	} else {
		newpair = ht_newpair( key, value );

		/* We're at the start of the linked list in this bin. */
		if( next == hashtable->table[ bin ] ) {
			newpair->next = next;
			hashtable->table[ bin ] = newpair;

		/* We're at the end of the linked list in this bin. */
		} else if ( next == NULL ) {
			last->next = newpair;

		/* We're in the middle of the list. */
		} else  {
			newpair->next = next;
			last->next = newpair;
		}
	}
}

/* Retrieve a key-value pair from a hash table. */
char *ht_get( hashtable_t *hashtable, char *key ) {
	int bin = 0;
	entry_t *pair;

	bin = ht_hash( hashtable, key );

	/* Step through the bin, looking for our value. */
	pair = hashtable->table[ bin ];
	while( pair != NULL && pair->key != NULL && strcmp( key, pair->key ) > 0 ) {
		pair = pair->next;
	}

	/* Did we actually find anything? */
	if( pair == NULL || pair->key == NULL || strcmp( key, pair->key ) != 0 ) {
		return NULL;

	} else {
		return pair->value;
	}

}
#endif
