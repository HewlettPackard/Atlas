/*
 * (c) Copyright 2016 Hewlett Packard Enterprise Development LP
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version. This program is
 * distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details. You should have received a copy of the GNU Lesser
 * General Public License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Source code is partially derived from Boehm-Demers-Weiser Garbage 
 * Collector (BDWGC) version 7.2 (license is attached)
 *
 * File:
 *   gc_inline.h
 *
 *   Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 *   Copyright (c) 1991-1995 by Xerox Corporation.  All rights reserved.
 *   Copyright (c) 2005 Hewlett-Packard Development Company, L.P.
 *
 *   THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY EXPRESSED
 *   OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.
 *
 *   Permission is hereby granted to use or copy this program
 *   for any purpose,  provided the above notices are retained on all copies.
 *   Permission to modify the code and to distribute modified code is granted,
 *   provided the above notices are retained, and a notice that the code was
 *   modified is included with the above copyright notice.
 *
 *   thread_local_alloc.h
 * 
 *   Copyright (c) 2000-2005 by Hewlett-Packard Company.  All rights reserved.
 *
 *   THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY EXPRESSED
 *   OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.
 *
 *   Permission is hereby granted to use or copy this program
 *   for any purpose,  provided the above notices are retained on all copies.
 *   Permission to modify the code and to distribute modified code is granted,
 *   provided the above notices are retained, and a notice that the code was
 *   modified is included with the above copyright notice. 
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

