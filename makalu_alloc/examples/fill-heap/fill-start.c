#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>


#include "region_manager.h"
#include "makalu.h"

#define NUM_PERSISTENT_ROOTS (4096 / sizeof(void*))

#define NUM_CHILD 8
typedef struct Node {
    //64 bytes 
    struct Node* next[NUM_CHILD];
}Node;

Node* P_roots[NUM_PERSISTENT_ROOTS];


void initialize()
{
   long i;
   for (i = 0; i < NUM_PERSISTENT_ROOTS; i++)
   {
       Node* n = (Node*) MAK_malloc(sizeof(Node));
       void** r_addr = MAK_persistent_root_addr(i);
       *r_addr = (void*) n;
       CLFLUSH(r_addr);
       P_roots[i] = n;
   }
}

unsigned long largest_depth(unsigned long* depth)
{
    unsigned long ret = 0;
    unsigned long i = 0;
    for (i = 0; i < NUM_PERSISTENT_ROOTS; i++)
        ret = depth[i] > ret ? depth[i] : ret;
    return ret;
}


int main(int argc, char *argv[])
{

    __map_persistent_region();
    void* hstart = MAK_start(&__nvm_region_allocator);
    __store_heap_start(hstart);

    //random number initialization
   if (argc < 3) {
        printf("Usage start_online_xx [#Mibytes][#crash_at_Mibytes]\n");
        return 0;
    }
    unsigned long mibytes_to_allocate = atol(argv[1]);
    unsigned long mibytes_before_abort = atol(argv[2]);

    unsigned long num_items = (mibytes_to_allocate * 1024 * 1024) 
                              / (sizeof (Node));
    unsigned long items_before_abort = (mibytes_before_abort * 1024 * 1024)
                                      / (sizeof (Node));

    printf("Executing for %ld items, crashing at %ld items\n", 
           num_items, items_before_abort);

    long int seed = (long int) (time(NULL));
    struct drand48_data rbuf;
    srand48_r(seed, &rbuf);

    initialize();
    
    unsigned long depth[NUM_PERSISTENT_ROOTS];
    memset(depth, 0, sizeof(depth));

    unsigned long n_attached = 0;
    unsigned long n_freed = 0;
    long random;
    unsigned long should_retain, attach_to, next_pos;
    volatile Node *next = NULL;
    Node *curr = NULL;
    int i;

    for (i=0; i < num_items; i++)
    {
        lrand48_r(&rbuf, &random);
        should_retain = random % 2; 
        if (should_retain){
            lrand48_r(&rbuf, &random);
            attach_to = random % ((unsigned long) NUM_PERSISTENT_ROOTS);
            lrand48_r(&rbuf, &random);
            next_pos =  random % ((unsigned long) NUM_CHILD);
	}
        next = (Node*) MAK_malloc(sizeof(Node));
        if (!should_retain)
        {
            memset((void*) next, 0, sizeof(Node));
            MAK_free((void*)next);
            //printf("Freeing %p\n", next);
            n_freed++;
        }
        else
        {
            curr = P_roots[attach_to] -> next[0];
            next->next[next_pos] = curr;
            CLFLUSH(&(next -> next[next_pos]));
            P_roots[attach_to] -> next[0] = (Node*) next; 
            CLFLUSH(&(P_roots[attach_to] -> next[0]));
            //printf("Attaching %p, from %ldth root\n", next, attach_to);
            n_attached++;
            depth[attach_to]++;
            //flush P_roots next[0] field
        }

        if (items_before_abort != 0 && i >= items_before_abort -1)
        {
            
            printf("Attached: %ld\n", n_attached);
            printf("Freed: %ld\n", n_freed);
            printf("Largest depth: %ld\n", largest_depth(depth));
            printf( "Aborting...\n");
            exit(1);
        }
    }

    return 0;    

}
