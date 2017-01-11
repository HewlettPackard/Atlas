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
 *   thread_local_alloc.c
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

#include "makalu_local_heap.h"

#ifdef MAK_THREADS

static word tlfs_max_count[TINY_FREELISTS] = {0};
static word tlfs_optimal_count[TINY_FREELISTS] = {0};
static word total_local_fl_size = 0;
static MAK_tlfs tlfs_fl = NULL;
static __thread MAK_tlfs my_tlfs = NULL;
static __thread MAK_bool never_allocates = FALSE;


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

STATIC void free_tlfs(MAK_tlfs tlfs)
{
    MAK_LOCK();
    tlfs -> next = tlfs_fl;
    tlfs_fl = tlfs;
    MAK_UNLOCK();
}

MAK_INNER MAK_tlfs MAK_set_my_thread_local(void)
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
    /* That explicit deallocation works. */
 
    flh = &(tlfs -> ptrfree_freelists[0]);
    flh->fl = NULL;
    flh->count = (signed_word)(-(tlfs_optimal_count[1]));
    flh = &(tlfs -> normal_freelists[0]);
    flh->fl = NULL;
    flh->count = (signed_word)(-(tlfs_optimal_count[1]));
    

   my_tlfs =  tlfs;

   return tlfs;
}


STATIC void return_freelists(fl_hdr *fl,
      hdr_cache_entry* hc, void** aflush_tb, int k)
{
    int i;
    for (i = 1; i < TINY_FREELISTS; ++i) {
        if (fl[i].count > 0) {
            MAK_truncate_fast_fl(
              &(fl[i]), (word) i, k,
              0, tlfs_max_count[i],
              hc, (word) LOCAL_HDR_CACHE_SZ,
              aflush_tb, 
              (word) LOCAL_AFLUSH_TABLE_SZ);
        }
    }
    if (fl[0].count > 0) {
        MAK_truncate_fast_fl(
            &(fl[0]), (word) 1, k,
            0, tlfs_max_count[1],
            hc, (word) LOCAL_HDR_CACHE_SZ,
            aflush_tb, 
            (word) LOCAL_AFLUSH_TABLE_SZ);
    }
}

MAK_INNER void MAK_teardown_thread_local(MAK_tlfs p)
{
    return_freelists(p -> ptrfree_freelists, p -> hc, p -> aflush_tb, PTRFREE);
    return_freelists(p -> normal_freelists, p -> hc, p -> aflush_tb, NORMAL);
    
    MAK_FLUSH_ALL_ENTRY(p -> aflush_tb, LOCAL_AFLUSH_TABLE_SZ);
    BZERO(p -> hc, sizeof(p -> hc));

    my_tlfs = NULL;
    free_tlfs(p);
}

/* Must be called from the main thread  */
/* for correct behavior */
/* Relies on main's thread local variable */
MAK_INNER void MAK_teardown_main_thread_local(void)
{
    MAK_teardown_thread_local(my_tlfs);
}

MAK_API void MAK_CALL MAK_declare_never_allocate(int flag){
    never_allocates = (flag != 0) ? TRUE : FALSE;
}


MAK_API void * MAK_CALL MAK_malloc(size_t bytes)
{
    size_t granules = ROUNDED_UP_GRANULES(bytes);
    void *result;
    fl_hdr *tiny_fl;

    if (EXPECT(my_tlfs == NULL, 0))
    {
        return MAK_core_malloc(bytes);
    }

    if (!MAK_is_initialized)
        ABORT("Makalu not properly initialized!\n");
   
    tiny_fl = (my_tlfs) -> normal_freelists;
    MAK_FAST_MALLOC_GRANS(result, granules, tiny_fl,
              (my_tlfs) -> hc, (my_tlfs) -> aflush_tb,
              NORMAL, MAK_core_malloc(bytes), tlfs_max_count[granules]);

    return result;
}

#ifdef FAS_SUPPORT

MAK_INNER MAK_fas_free_callback  MAK_fas_defer_free_fn = 0;

MAK_API void MAK_CALL MAK_set_defer_free_fn(MAK_fas_free_callback fn){
    MAK_fas_defer_free_fn = fn;
}


MAK_API void MAK_CALL MAK_free(void *p){
    MAK_fas_defer_free_fn(pthread_self(), p);
}

MAK_API void MAK_CALL MAK_free_imm(void *p){

#else

MAK_API void MAK_CALL MAK_free(void *p){

#endif

    int add_to_gfl = 0;
    if (EXPECT(my_tlfs == NULL, 0))
    {
         add_to_gfl = 1;
    }
    if (!MAK_is_initialized)
        ABORT("Makalu not properly initialized!\n");


    hdr *hhdr;
    size_t sz; /* In bytes */
    size_t granules;   /* sz in granules */
    fl_hdr *tiny_fl;
    int knd;
    struct obj_kind * ok;
    
    if (p == 0) return;
    /* Required by ANSI.  It's not my fault ...     */
    
    void* r = MAK_hc_base_with_hdr(p, (my_tlfs) -> hc,
                  (unsigned int) LOCAL_HDR_CACHE_SZ, &hhdr);
    if (r == 0) return;

    sz = hhdr -> hb_sz;
    knd = hhdr -> hb_obj_kind;
    granules = BYTES_TO_GRANULES(sz);
    if (EXPECT(((granules) >= TINY_FREELISTS) || add_to_gfl, FALSE)) {
        MAK_core_free(r, hhdr, knd, sz, granules);
        return;
    }

    if (knd == NORMAL) {
       tiny_fl = (my_tlfs) -> normal_freelists;
    }
    else if (knd == PTRFREE){
       tiny_fl = (my_tlfs) -> ptrfree_freelists;
    }
    else {
        MAK_core_free(r, hhdr, knd, sz, granules);
        return;
    }
    fl_hdr *flh = tiny_fl + granules;
    signed_word count = flh -> count;
   #ifndef START_THREAD_LOCAL_IMMEDIATE
    if (count < 0) {
        flh -> count = count + 1;
        MAK_core_free(r, hhdr, knd, sz, granules);
        return;
    }
   #endif
    void **flp;
    flp = flh -> fl;
    ok = &MAK_obj_kinds[knd];
    if(ok -> ok_init){
        BZERO((word *)p, sz);
    }
    word idx = (flh -> start_idx + count) % tlfs_max_count[granules];
    flp[idx] = p;
    count++;
    flh -> count = count;

    if (count >= tlfs_max_count[granules]){
        word keep = (never_allocates == TRUE) ? 
            0 : tlfs_optimal_count[granules];
        MAK_truncate_fast_fl(
           (fl_hdr*) flh, granules, knd,
              keep, tlfs_max_count[granules],
              (my_tlfs) -> hc, (word) 
              LOCAL_HDR_CACHE_SZ,
              (my_tlfs) -> aflush_tb, 
              (word) LOCAL_AFLUSH_TABLE_SZ);
    }
}

#endif // MAK_THREADS
