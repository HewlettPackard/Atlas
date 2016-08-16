#ifdef MAK_THREADS

#include "makalu_local_heap.h"

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

STATIC void MAK_alloc_tlfs()
{
    MAK_tlfs ret;
    LOCK();
    if (tlfs_fl != NULL){
        ret = tlfs_fl;
        tlfs_fl = ret -> next;
        UNLOCK();
    } else {
       word size = sizeof(struct thread_local_freelists);
       size += total_local_fl_size;
       ret = (MAK_tlfs) MAK_transient_scratch_alloc(size);
       UNLOCK();
       BZERO(ret, size);
    }
    return ret;
}

MAK_INNER void MAK_set_my_thread_local(void)
{
    MAK_tlfs tlfs = MAK_alloc_tlfs();
    if (tlfs == 0)
         ABORT("Failed to allocate memory for thread registering");

    int i;
    register local_fl_hdr *fl_hdr;
    void** fl_array = tlfs -> fl_array;
    word fl_array_idx = 0;
    for (i = 1; i < TINY_FREELISTS; ++i) {
        fl_hdr = &(p->ptrfree_freelists[i]);
        fl_hdr -> fl = fl_array + fl_array_idx;
       #ifdef START_THREAD_LOCAL_IMMEDIATE
        fl_hdr -> count = (signed_word)(0);
       #else
        fl_hdr -> count = (signed_word)(-(tlfs_optimal_count[i]));
       #endif
        fl_array_idx += tlfs_max_count[i];
    }

    for (i = 1; i < TINY_FREELISTS; ++i) {
        fl_hdr = &(p->normal_freelists[i]);
        fl_hdr->fl = fl_array + fl_array_idx;
       #ifdef START_THREAD_LOCAL_IMMEDIATE
        fl_hdr->count = (signed_word)(0);
       #else
        fl_hdr->count = (signed_word)(-(tlfs_optimal_count[i]));
       #endif
        fl_array_idx += tlfs_max_count[i];
    }

    /* Set up the size 0 free lists.    */
    /* We now handle most of them like regular free lists, to ensure    */
    /* That explicit deallocation works.  However, allocation of a      */
    /* size 0 "gcj" object is always an error.                          */
    fl_hdr = &(p -> ptrfree_freelists[0]);
    fl_hdr->fl = NULL;
    fl_hdr->count = (signed_word)(-(tlfs_optimal_count[1]));
    fl_hdr = &(p -> normal_freelists[0]);
    fl_hdr->fl = NULL;
    fl_hdr->count = (signed_word)(-(tlfs_optimal_count[1]));
    

    my_tlfs =  tlfs;
}

#endif
