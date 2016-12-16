/*
 * Source code is partially derived from Boehm-Demers-Weiser Garbage 
 * Collector (BDWGC) version 7.2 (license is attached)
 *
 * File:
 *   gc_inline.h
 *   thread_local_alloc.h
 */

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
MAK_INNER MAK_tlfs MAK_set_my_thread_local(void);
MAK_INNER void MAK_teardown_thread_local(MAK_tlfs p);


#if defined(START_THREAD_LOCAL_IMMEDIATE)

    #define CHECK_CORE_MALLOC_START(flh, default_expr, result, count, out) 
    #define CHECK_CORE_MALLOC_END

#else // ! START_THREAD_LOCAL_IMMEDIATE

    #define CHECK_CORE_MALLOC_START(flh, default_expr, result, count, out) \
    if (count < 0) { \
    /* Small counter value, not NULL */ \
        flh -> count = count + 1; \
          result = (default_expr); \
            goto out; \
        } else {

    #define CHECK_CORE_MALLOC_END \
        }

#endif   //START_THREAD_LOCAL_IMMEDIATE

#define MAK_FAST_MALLOC_GRANS(result,granules,tiny_fl,\
               hc, aflush_tb, kind,default_expr, max_count) \
{ \
    if (EXPECT((granules) >= TINY_FREELISTS,0)) { \
        result = (default_expr); \
    } else { \
        fl_hdr* flh = &tiny_fl[granules]; \
        signed_word count = flh -> count; \
 \
        if (EXPECT(count <= 0, 0)) { \
            /* Entry contains counter or NULL */ \
                /* Large counter or NULL */ \
            CHECK_CORE_MALLOC_START(flh, default_expr, result, count, out) \
                MAK_generic_malloc_many(((granules) == 0 ? GRANULE_BYTES : \
                                        RAW_BYTES_FROM_INDEX(granules)), \
                                       kind, (fl_hdr*) flh, \
                                       hc, LOCAL_HDR_CACHE_SZ, \
                                       aflush_tb, LOCAL_AFLUSH_TABLE_SZ); \
                flh -> start_idx = 0; \
                count = flh -> count; \
                if (count == 0) { \
                    result = 0; \
                    goto out; \
                } \
            CHECK_CORE_MALLOC_END \
        } \
        word start = flh -> start_idx; \
        signed_word new_count = count - 1; \
        word idx = (start + (word) new_count) % max_count; \
        void** flp = flh -> fl; \
        result =  (void*) (flp[idx]); \
        flh -> count = new_count; \
        PREFETCH_FOR_WRITE(result); \
        MAK_ASSERT(MAK_size(result) >= (granules)*MAK_GRANULE_BYTES); \
        MAK_ASSERT((kind) == PTRFREE || ((MAK_word *)result)[1] == 0); \
      out: ; \
   } \
}



#endif   // MAK_THREADS

#endif // _MAKALU_LOCAL_HEAP

