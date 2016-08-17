
#include "makalu_local_heap.h"

#ifdef MAK_THREADS

static word tlfs_max_count[TINY_FREELISTS] = {0};
static word tlfs_optimal_count[TINY_FREELISTS] = {0};
static word total_local_fl_size = 0;
static MAK_tlfs tlfs_fl = NULL;
static __thread MAK_tlfs my_tlfs = NULL;


MAK_INNER void MAK_init_thread_local()
{
    word total = 0;
    int i;
    for (i = 1; i < TINY_FREELISTS; i++){
        tlfs_max_count[i] = (word) LOCAL_FL_MAX_SIZE / (word) (i * GRANULE_BYTES);
        total += tlfs_max_count[i];
        tlfs_optimal_count[i] = (word) LOCAL_FL_OPTIMAL_SIZE / (word) (i * GRANULE_BYTES);
    }
   
    total_local_fl_size = 2 * total * sizeof (void*);
}

STATIC MAK_tlfs alloc_tlfs()
{
    MAK_tlfs ret;
    MAK_LOCK();
    if (tlfs_fl != NULL){
        ret = tlfs_fl;
        tlfs_fl = ret -> next;
        MAK_UNLOCK();
    } else {
       word size = sizeof(struct thread_local_freelists);
       size += total_local_fl_size;
       ret = (MAK_tlfs) MAK_transient_scratch_alloc(size);
       MAK_UNLOCK();
       BZERO(ret, size);
    }
    return ret;
}

MAK_INNER void MAK_set_my_thread_local(void)
{
    MAK_tlfs tlfs = alloc_tlfs();
    if (tlfs == 0)
         ABORT("Failed to allocate memory for thread registering");

    int i;
    register fl_hdr *flh;
    void** fl_array = tlfs -> fl_array;
    word fl_array_idx = 0;
    for (i = 1; i < TINY_FREELISTS; ++i) {
        flh = &(tlfs -> ptrfree_freelists[i]);
        flh -> fl = fl_array + fl_array_idx;
       #ifdef START_THREAD_LOCAL_IMMEDIATE
        flh -> count = (signed_word)(0);
       #else
        flh -> count = (signed_word)(-(tlfs_optimal_count[i]));
       #endif
        fl_array_idx += tlfs_max_count[i];
    }

    for (i = 1; i < TINY_FREELISTS; ++i) {
        flh = &(tlfs -> normal_freelists[i]);
        flh->fl = fl_array + fl_array_idx;
       #ifdef START_THREAD_LOCAL_IMMEDIATE
        flh->count = (signed_word)(0);
       #else
        flh->count = (signed_word)(-(tlfs_optimal_count[i]));
       #endif
        fl_array_idx += tlfs_max_count[i];
    }

    /* Set up the size 0 free lists.    */
    /* We now handle most of them like regular free lists, to ensure    */
    /* That explicit deallocation works.  However, allocation of a      */
    /* size 0 "gcj" object is always an error.                          */
    flh = &(tlfs -> ptrfree_freelists[0]);
    flh->fl = NULL;
    flh->count = (signed_word)(-(tlfs_optimal_count[1]));
    flh = &(tlfs -> normal_freelists[0]);
    flh->fl = NULL;
    flh->count = (signed_word)(-(tlfs_optimal_count[1]));
    

    my_tlfs =  tlfs;
}

#endif
