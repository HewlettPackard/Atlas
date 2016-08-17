#ifndef _MAKALU_LOCAL_HEAP
#define _MAKALU_LOCAL_HEAP


#include "makalu_internal.h"

#ifdef MAK_THREADS


typedef struct thread_local_freelists {

    fl_hdr ptrfree_freelists[TINY_FREELISTS];
    fl_hdr normal_freelists[TINY_FREELISTS];
    
    hdr_cache_entry hc[LOCAL_HDR_CACHE_SZ];
    void* aflush_tb[LOCAL_AFLUSH_TABLE_SZ];

    /*To create a list of free MAK_tlfs */
    struct thread_local_freelists* next;

    /* the space for granule freelist */
    void* fl_array[0]; 

} *MAK_tlfs;

MAK_INNER void MAK_init_thread_local();
MAK_INNER void MAK_set_my_thread_local(void);

#endif

#endif

