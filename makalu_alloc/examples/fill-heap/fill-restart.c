#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>


#include "region_manager.h"
#include "makalu.h"

/* Fill-restart restarts the existing heap */
/* after recovery (fill-recover) or after  */
/* a clean shutdown  (fill-restart) */

#define NUM_PERSISTENT_ROOTS (4096 / sizeof(void*))

#define NUM_CHILD 8
typedef struct Node {
    //64 bytes 
    struct Node* next[NUM_CHILD];
}Node;

Node* P_roots[NUM_PERSISTENT_ROOTS];


void reinitialize()
{
   long i;
   for (i = 0; i < NUM_PERSISTENT_ROOTS; i++)
   {
       Node* n = (Node*) MAK_persistent_root(i);
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
   if (argc < 2) {
        printf("Usage: fill-restart [#total_MB]\n");
        return 0;
    }

    unsigned long mibytes_to_allocate = atol(argv[1]);

    unsigned long num_items = (mibytes_to_allocate * 1024 * 1024) 
                              / (sizeof (Node));


    printf("Executing for %ld items\n", 
           num_items);

    long int seed = (long int) (time(NULL));
    unsigned long n_attached = 0;
    unsigned long n_freed = 0;
    long random;
    unsigned long should_retain, attach_to, next_pos;
    volatile Node *next = NULL;
    Node *curr = NULL;

    struct drand48_data rbuf;
    srand48_r(seed, &rbuf);

    __remap_persistent_region();
    void* hstart = __fetch_heap_start();

    printf("Heap start address: %p\n", hstart);


    MAK_restart(hstart, &__nvm_region_allocator);

    reinitialize();
    
    unsigned long depth[NUM_PERSISTENT_ROOTS];
    memset(depth, 0, sizeof(depth));

    
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
            n_freed++;
        }
        else
        {
            curr = P_roots[attach_to] -> next[0];
            next->next[next_pos] = curr;
            CLFLUSH(&(next -> next[next_pos]));
            P_roots[attach_to] -> next[0] = (Node*) next; 
            CLFLUSH(&(P_roots[attach_to] -> next[0]));
            n_attached++;
            depth[attach_to]++;
        }
    }


    MAK_close();
    __close_persistent_region();

    printf("Attached: %ld\n", n_attached);
    printf("Freed: %ld\n", n_freed);
    printf("Largest depth: %ld\n", largest_depth(depth));

    return 0;    

}
