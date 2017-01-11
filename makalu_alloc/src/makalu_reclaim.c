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
 *   reclaim.c
 *
 *   Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 *   Copyright (c) 1991-1996 by Xerox Corporation.  All rights reserved.
 *   Copyright (c) 1996-1999 by Silicon Graphics.  All rights reserved.
 *   Copyright (c) 1999-2004 Hewlett-Packard Development Company, L.P.
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

#include "makalu_internal.h"

MAK_INNER MAK_bool MAK_alloc_reclaim_list(unsigned k /*kind */)
{
    struct hblk** result;
    result = (struct hblk **) MAK_transient_scratch_alloc(
              (MAXOBJGRANULES+1) * sizeof(struct hblk *));
    if (result == 0) return(FALSE);
    BZERO(result, (MAXOBJGRANULES+1)*sizeof(struct hblk *));
    MAK_reclaim_list[k] = result;
   
    return (TRUE);
}

STATIC MAK_bool MAK_block_nearly_full(hdr *hhdr)
{
    return (hhdr -> hb_n_marks > BLOCK_NEARLY_FULL_THRESHOLD(hhdr -> hb_sz));
}

MAK_INNER void MAK_restart_block_freelists()
{

    signed_word j;
    bottom_index * index_p;
    struct hblk* h;
    hdr* hhdr;
    word n_blocks;
    int fl_index;
    struct hblk* second;
    hdr* second_hdr;
    word sz;
    unsigned obj_kind;
    struct hblk ** rlh;

    for (index_p = MAK_all_bottom_indices; index_p != 0;
         index_p = index_p -> asc_link) {
        for (j = BOTTOM_SZ-1; j >= 0;) {
            if (!IS_FORWARDING_ADDR_OR_NIL(index_p->index[j])) {
                hhdr = (hdr*) (index_p->index[j]);
                h = hhdr -> hb_block;
                sz = hhdr -> hb_sz;
                if (HBLK_IS_FREE(hhdr)) {
                    //build hblk freelist
                    n_blocks = divHBLKSZ(sz);
                    if (n_blocks <= UNIQUE_THRESHOLD)
                        fl_index = n_blocks;
                    else if (n_blocks >= HUGE_THRESHOLD)
                        fl_index = N_HBLK_FLS;
                    else
                        fl_index = (int)(n_blocks - UNIQUE_THRESHOLD)/FL_COMPRESSION
                                        + UNIQUE_THRESHOLD;
                    MAK_free_bytes[fl_index] += sz;
                    second = MAK_hblkfreelist[fl_index];
                    MAK_hblkfreelist[fl_index] = h;
                    hhdr -> hb_next = second;
                    hhdr -> hb_prev = 0;
                    if (second != 0){
                        second_hdr = HDR(second);
                        second_hdr -> hb_prev = h;
                    }
                }
                else {
                   //build reclaim list
                   MAK_update_hc(h -> hb_body, hhdr, MAK_hdr_cache, (word) HDR_CACHE_SIZE);
                   obj_kind = hhdr -> hb_obj_kind;
                   if ( sz <=  MAXOBJBYTES && !MAK_block_nearly_full(hhdr)) {
                       rlh = &(MAK_reclaim_list[obj_kind][BYTES_TO_GRANULES(sz)]);
                       second = *rlh;
                       hhdr -> hb_next =  second;
                       hhdr -> hb_prev = NULL;
                       if (second != NULL){
                           second_hdr = MAK_get_hdr_and_update_hc(second -> hb_body,
                                                      MAK_hdr_cache, (word) HDR_CACHE_SIZE);
                           second_hdr -> hb_prev = h;
                       }
                       hhdr -> page_reclaim_state = IN_RECLAIMLIST;
                       *rlh = h;
                   }
                }
                j--;
             } else if (index_p->index[j] == 0) {  //is nil
                j--;
             } else {   //is forwarding address
                j -= (signed_word)(index_p->index[j]);
             }
         }
     }
}

/*
 * Restore an unmarked large object or an entirely empty blocks of small objects
 * to the heap block free list.
 * Otherwise process the block for later reclaiming
 * by MAK_reclaim_small_nonempty_block (lazy sweeping).
 */
MAK_INNER void MAK_defer_reclaim_block(struct hblk *hbp, word flag)
{
    hdr * hhdr = HDR(hbp);
    size_t sz = hhdr -> hb_sz;  /* size of objects in current block     */

    //TODO: cache the header in apply to all blocks and use header
    //cache here.
    if( sz > MAXOBJBYTES ) {  /* 1 big object */
        if( !mark_bit_from_hdr(hhdr, 0) ) {
            MAK_START_NVM_ATOMIC;
            MAK_freehblk(hbp);
            MAK_END_NVM_ATOMIC;
        } else {
            //flush mark bits and the number of marks
            MAK_NVM_ASYNC_RANGE(&(hhdr -> hb_marks[0]), sizeof (hhdr -> hb_marks));
            MAK_NVM_ASYNC_RANGE(&(hhdr -> hb_n_marks), sizeof(hhdr -> hb_n_marks));
        }
    } else {
        MAK_bool empty = MAK_block_empty(hhdr);
       #ifdef MAK_THREADS
        /* Count can be low or one too high because we sometimes      */
        /* have to ignore decrements.  Objects can also potentially   */
        /* be repeatedly marked by each marker.                       */
        /* Here we assume two markers, but this is extremely          */
        /* unlikely to fail spuriously with more.  And if it does, it */
        /* should be looked at.                                       */
        MAK_ASSERT(hhdr -> hb_n_marks <= 2 * (HBLKSIZE/sz + 1) + 16);
       #else
        MAK_ASSERT(sz * hhdr -> hb_n_marks <= HBLKSIZE);
       #endif
        if (empty) {
          MAK_START_NVM_ATOMIC;
          MAK_freehblk(hbp);
          MAK_END_NVM_ATOMIC;
        } else {
           //TODO: flush the mark bits and the number of marks
           MAK_NVM_ASYNC_RANGE(&(hhdr -> hb_marks[0]), sizeof (hhdr -> hb_marks));
           MAK_NVM_ASYNC_RANGE(&(hhdr -> hb_n_marks), sizeof(hhdr -> hb_n_marks));
        }
    }
}

MAK_INNER signed_word MAK_build_array_fl(struct hblk *h, size_t sz_in_words, MAK_bool clear,
                           hdr_cache_entry* hc, word hc_sz,
                           void** aflush_tb, word aflush_tb_sz,
                           void** list)
{
    word *p;
    word *last_object;            /* points to last object in new hblk    */
    signed_word idx = 0;
    hdr* hhdr = MAK_get_hdr_and_update_hc(h -> hb_body, hc, hc_sz); 
    
    /* Do a few prefetches here, just because its cheap.          */
    /* If we were more serious about it, these should go inside   */
    /* the loops.  But write prefetches usually don't seem to     */
    /* matter much.                                               */
    PREFETCH_FOR_WRITE((ptr_t)h);
    PREFETCH_FOR_WRITE((ptr_t)h + 128);
    PREFETCH_FOR_WRITE((ptr_t)h + 256);
    PREFETCH_FOR_WRITE((ptr_t)h + 378);

    if (clear) BZERO(h, HBLKSIZE);

    p = (word *)(h -> hb_body); /*first object in *h  */
    last_object = (word *)((char *)h + HBLKSIZE);
    last_object -= sz_in_words;
    /* Last place for last object to start */

    /* make a list of all objects in *h with head as last object */
    while (p <= last_object) {
       /* current object's link points to last object */
        list[idx] = (void*) p;
        idx++;
        p += sz_in_words;
    }

    size_t sz = hhdr -> hb_sz;
    unsigned i;
    for (i = 0; i < MARK_BITS_SZ; ++i) {
        hhdr -> hb_marks[i] = ONES;
    }
    hhdr -> hb_n_marks = HBLK_OBJS(sz);
    hhdr -> page_reclaim_state = IN_FLOATING;
    MAK_NVM_ASYNC_RANGE_VIA(&(hhdr -> hb_marks), sizeof (hhdr -> hb_marks),
                           aflush_tb, aflush_tb_sz);
    MAK_NVM_ASYNC_RANGE_VIA(&(hhdr -> hb_n_marks), sizeof (hhdr -> hb_n_marks),
                           aflush_tb, aflush_tb_sz);
    return idx;
}


STATIC signed_word MAK_reclaim_to_array_clear(struct hblk *hbp, hdr *hhdr, size_t sz,
                              void** aflush_tb, word aflush_tb_sz,
                              void** list)
{

    word bit_no = 0;
    word *p, *q, *plim;
    signed_word n_bytes_found = 0;
    signed_word idx = 0;

    //MAK_ASSERT(hhdr == MAK_find_header((ptr_t)hbp));
    MAK_ASSERT(sz == hhdr -> hb_sz);
    MAK_ASSERT((sz & (BYTES_PER_WORD-1)) == 0);
    p = (word *)(hbp->hb_body);
    plim = (word *)(hbp->hb_body + HBLKSIZE - sz);

    /* go through all words in block */
    while (p <= plim) {
        if( mark_bit_from_hdr(hhdr, bit_no) ) {
            p = (word *)((ptr_t)p + sz);
        } else {
            n_bytes_found += sz;
          /* object is available - put on list */
            list[idx] = p;
            idx++;
          /* Clear object, advance p to next object in the process */
            q = (word *)((ptr_t)p + sz);
            while (p < q) {
              *p++ = 0;
            }
            set_mark_bit_from_hdr_unsafe(hhdr, bit_no);
        }
        bit_no += MARK_BIT_OFFSET(sz);
    }  
    hhdr -> hb_n_marks = HBLK_OBJS(sz);
    hhdr -> page_reclaim_state = IN_FLOATING;    
    if (n_bytes_found != 0){
        MAK_NVM_ASYNC_RANGE_VIA(&(hhdr -> hb_n_marks), 
           sizeof(hhdr -> hb_n_marks), aflush_tb, aflush_tb_sz);
        MAK_NVM_ASYNC_RANGE_VIA(&(hhdr -> hb_marks), 
           sizeof(hhdr -> hb_marks), aflush_tb, aflush_tb_sz);
    }
    return idx;
}

 
/* The same thing, but don't clear objects: */
/* returns the number of memory objects reclaimed/added to freelist */
STATIC signed_word MAK_reclaim_to_array_uninit(struct hblk *hbp, hdr *hhdr, size_t sz,
                               void** aflush_tb, word aflush_tb_sz,
                               void** list)
{

    word bit_no = 0;
    word *p, *plim;
    signed_word n_bytes_found = 0;
    signed_word idx = 0;


    MAK_ASSERT(sz == hhdr -> hb_sz);
    p = (word *)(hbp->hb_body);
    plim = (word *)((ptr_t)hbp + HBLKSIZE - sz);

    /* go through all words in block */
    while (p <= plim) {
       if( !mark_bit_from_hdr(hhdr, bit_no) ) {
           n_bytes_found += sz;
           /* object is available - put on list */
           list[idx]= p;
           idx++;
           set_mark_bit_from_hdr_unsafe(hhdr, bit_no);
       } 
       p = (word *)((ptr_t)p + sz);
       bit_no += MARK_BIT_OFFSET(sz);
    }
    hhdr -> hb_n_marks = HBLK_OBJS(sz);

    hhdr -> page_reclaim_state = IN_FLOATING;

    if (n_bytes_found != 0){
        MAK_NVM_ASYNC_RANGE_VIA(&(hhdr -> hb_n_marks), sizeof(hhdr -> hb_n_marks),
                     aflush_tb, aflush_tb_sz);
        MAK_NVM_ASYNC_RANGE_VIA(&(hhdr -> hb_marks), sizeof(hhdr -> hb_marks),
                     aflush_tb, aflush_tb_sz);
    }
    return idx;
}

MAK_INNER signed_word MAK_build_fl_array_from_mark_bits_clear(struct hblk* hbp,
       word* mark_bits,
       size_t sz,
       void** list)
{
    word bit_no = 0;
    word *p, *q, *plim;
    signed_word n_bytes_found = 0;
    signed_word idx = 0;

    p = (word *)(hbp->hb_body);
    plim = (word *)(hbp->hb_body + HBLKSIZE - sz);

    while (p <= plim) {
        if( mark_bit_from_mark_bits(mark_bits, bit_no) ) {
            p = (word *)((ptr_t)p + sz);
        } else {
            n_bytes_found += sz;
          /* object is available - put on list */
            list[idx] = p;
            idx++;
          /* Clear object, advance p to next object in the process */
            q = (word *)((ptr_t)p + sz);
#        ifdef USE_MARK_BYTES
            MAK_ASSERT(!(sz & 1)
               && !((word)p & (2 * sizeof(word) - 1)));
            while (p < q) {
              CLEAR_DOUBLE(p);
              p += 2;
            }
#        else
           #ifdef CLEAR_MALLOCED_MEMORY
            while (p < q) {
              *p++ = 0;
            }
           #else
               p = q;
           #endif
#        endif
        }
        bit_no += MARK_BIT_OFFSET(sz);
    }
    return idx;
}


MAK_INNER signed_word MAK_build_fl_array_from_mark_bits_uninit(struct hblk* hbp,
       word* mark_bits,
       size_t sz,
       void** list)
{
    word bit_no = 0;
    word *p, *plim;
    signed_word n_bytes_found = 0;
    signed_word idx = 0;


    MAK_ASSERT(sz == hhdr -> hb_sz);
    p = (word *)(hbp->hb_body);
    plim = (word *)((ptr_t)hbp + HBLKSIZE - sz);

    /* go through all words in block */
    while (p <= plim) {
       if(!mark_bit_from_mark_bits(mark_bits, bit_no) ) {
           n_bytes_found += sz;
           /* object is available - put on list */
           list[idx]= p;
           idx++;
       }
       p = (word *)((ptr_t)p + sz);
       bit_no += MARK_BIT_OFFSET(sz);
    }
    return idx;
}




MAK_INNER signed_word MAK_reclaim_to_array_generic(struct hblk * hbp,
                                  hdr *hhdr,
                                  size_t sz,
                                  MAK_bool init,
                                  hdr_cache_entry* hc,
                                  word hc_sz,
                                  void** aflush_tb,
                                  word aflush_tb_sz,
                                  void** list)
{
    signed_word result = 0;
    MAK_ASSERT(MAK_find_header((ptr_t)hbp) == hhdr);
    if (init) {
      result = MAK_reclaim_to_array_clear(hbp, hhdr, sz,
                            aflush_tb, aflush_tb_sz,
                            list);
    } else {
      MAK_ASSERT((hhdr)->hb_descr == 0 /* Pointer-free block */);
      result = MAK_reclaim_to_array_uninit(hbp, hhdr, sz,
                            aflush_tb, aflush_tb_sz,
                            list);
    }
    return result;
}



STATIC void MAK_reclaim_small_nonempty_block(struct hblk *hbp, hdr* hhdr)
{
    size_t sz = hhdr -> hb_sz;
    struct obj_kind * ok = &MAK_obj_kinds[hhdr -> hb_obj_kind];
    fl_hdr* flh = &(ok -> ok_freelist[BYTES_TO_GRANULES(sz)]);
    flh -> count = MAK_reclaim_to_array_generic(hbp,
                  hhdr,
                  sz,
                  ok -> ok_init,
                  &(MAK_hdr_cache[0]),
                  (word) HDR_CACHE_SIZE,
                  &(MAK_fl_aflush_table[0]),
                  (word) FL_AFLUSH_TABLE_SZ,
                  flh -> fl);
}

MAK_INNER void MAK_continue_reclaim(size_t gran /* granules */, int kind)
{
    hdr * hhdr;
    struct hblk * hbp;
    struct obj_kind * ok = &(MAK_obj_kinds[kind]);
    //first reclaim recent reclaim pages
    struct hblk ** rlh = MAK_reclaim_list[kind];
    if (rlh == 0) return;       /* No blocks of this kind.      */
    //TODO: should we check here that the fl_count is indeed zero.
    fl_hdr* flh = &(ok -> ok_freelist[gran]);
    rlh += gran;
    while ((hbp = *rlh) != 0) {

        hhdr = MAK_get_hdr_and_update_hc(hbp -> hb_body, MAK_hdr_cache, HDR_CACHE_SIZE);
        struct hblk* n_hb = hhdr -> hb_next;
        *rlh = n_hb;
        if (n_hb != NULL){
            hdr* n_hhdr = HDR(n_hb);
            n_hhdr -> hb_prev = NULL;
        }
        hhdr -> hb_next = NULL;
        MAK_reclaim_small_nonempty_block(hbp, hhdr);
        if (flh -> count > 0) return; 
    }
}

/* truncates freelist for a particular granule */
/* assumes that the granule lock is held */
MAK_INNER void MAK_truncate_freelist(fl_hdr* flh, word ngranules, int k,
                         word keep, word max,
                         hdr_cache_entry* hc, word hc_sz,
                         void** aflush_tb, word aflush_tb_sz)
{
    struct hblk** rlh = &(MAK_reclaim_list[k][ngranules]);
    size_t sz = GRANULES_TO_BYTES(ngranules);
    word full_threshold = (word) BLOCK_NEARLY_FULL_THRESHOLD(sz);


    signed_word t_count = flh -> count;
    signed_word k_count = keep < t_count ? (signed_word) keep : t_count;

    if (k_count == t_count){
        return;
    }


    word start = flh -> start_idx;
    signed_word n_truncated = t_count - k_count;


    hdr* hhdr;
    struct hblk* h;
    word bit_no;
    void* p;
    signed_word i;

    void** flp = flh -> fl;
    for (i = 0; i < n_truncated; i++){
        p =  flp[ (start + (word) i) % max ];
        hhdr = MAK_get_hdr_and_update_hc(p, hc, hc_sz);
        h = hhdr -> hb_block;
        bit_no = MARK_BIT_NO((ptr_t)p - (ptr_t) h, sz);

        if (!mark_bit_from_hdr(hhdr, bit_no))
        {
            WARN("Double deallocation detected\n", 0);
            //printf("someone else already deleted this item %p\n", p);
            //printf("Pointer: %p, hb_block: %p, bb_size: %lu\n", p,
            //       hhdr -> hb_block, (word) hhdr -> hb_sz);
            continue;
        }

        clear_and_flush_mark_bit_from_hdr(hhdr, bit_no, aflush_tb, aflush_tb_sz);
        --hhdr -> hb_n_marks;
        //if a page becomes completely empty, remove from the reclaim list
        //deallocate the page from that size class
        if (hhdr -> hb_n_marks == 0)
        {
            struct hblk* n_hb = hhdr -> hb_next;
            struct hblk* p_hb = hhdr -> hb_prev;
            if (n_hb != NULL){
                hdr* n_hhdr = MAK_get_hdr_and_update_hc(n_hb -> hb_body,
                                                 hc, hc_sz);
                n_hhdr -> hb_prev = p_hb;
                hhdr -> hb_next = NULL;
            }
            if (p_hb != NULL){
                hdr* p_hhdr = MAK_get_hdr_and_update_hc(p_hb -> hb_body,
                                                 hc, hc_sz);
                p_hhdr -> hb_next = n_hb;
                hhdr -> hb_prev = NULL;
            }

            if (h == *rlh){
                *rlh = n_hb;
            }

            MAK_LOCK();
            MAK_START_NVM_ATOMIC;
            MAK_freehblk(h);
            MAK_END_NVM_ATOMIC;
            MAK_UNLOCK();
            continue;
        }
        /* sufficient number of memory objects within the page are free */
        /* add it back to the reclaim list */
        else if (hhdr -> page_reclaim_state == IN_FLOATING
                        && hhdr -> hb_n_marks <= full_threshold)
        {
            hhdr -> page_reclaim_state = IN_RECLAIMLIST;
            struct hblk* top_hb = *rlh;
            hhdr -> hb_next = top_hb;
            hhdr -> hb_prev = NULL;
            if (top_hb != NULL){
                hdr* top_hhdr = MAK_get_hdr_and_update_hc(top_hb -> hb_body, hc, hc_sz);
                top_hhdr -> hb_prev = h;
            }
            *rlh = h;
        }

        MAK_NVM_ASYNC_RANGE_VIA(&(hhdr -> hb_n_marks),
            sizeof(hhdr -> hb_n_marks), aflush_tb, aflush_tb_sz);
    }
    flh -> count = k_count;
    flh -> start_idx = (start + (word) n_truncated) % max;
}

struct trun_tb_e {
    word marks[MARK_BITS_SZ];
    word mark_count;
    word block;
    hdr* hhdr;
};


STATIC void remove_trun_tb_entry(struct trun_tb_e* start, struct trun_tb_e*
    end, word gran, int kind, word full_thres,
    struct hblk** rlh, hdr_cache_entry* hc, word hc_sz,
    void** aflush_tb, word aflush_tb_sz)
{
    hdr* hhdr;
    struct hblk* h;
    struct trun_tb_e* entry;
    MAK_bool already_unlocked = FALSE;
    MAK_LOCK_GRAN(gran);

     for (entry = start; entry <= end; entry++)
    {
        if (EXPECT(entry -> block == 0, 0)) continue;
        hhdr = entry -> hhdr;
        h = hhdr -> hb_block;
        hhdr -> hb_n_marks -= entry -> mark_count;
        if (EXPECT(hhdr -> hb_n_marks == 0, 1))
        {
            if (EXPECT(hhdr -> page_reclaim_state == IN_RECLAIMLIST, 0))
            {
                struct hblk* n_hb = hhdr -> hb_next;
                struct hblk* p_hb = hhdr -> hb_prev;
                if (n_hb != NULL){
                    hdr* n_hhdr =
                     MAK_get_hdr_and_update_hc(n_hb -> hb_body,
                         hc, hc_sz);
                     n_hhdr -> hb_prev = p_hb;
                     hhdr -> hb_next = NULL;
                }
                if (p_hb != NULL){
                    hdr* p_hhdr =
                     MAK_get_hdr_and_update_hc(p_hb -> hb_body,
                         hc, hc_sz);
                    p_hhdr -> hb_next = n_hb;
                    hhdr -> hb_prev = NULL;
                }

                if (h == *rlh){
                    *rlh = n_hb;
                }
            }
            MAK_UNLOCK_GRAN(gran);
            MAK_LOCK();
            MAK_START_NVM_ATOMIC;
            MAK_freehblk(h);
            MAK_END_NVM_ATOMIC;
            MAK_UNLOCK();
            entry -> block = 0;

            //we have some more to process 
            //so we have to lock
            if (entry != end){
               MAK_LOCK_GRAN(gran);
            }
            else{
                already_unlocked = TRUE;
            }
            continue;
        }
        int i;
        for (i = 0; i < MARK_BITS_SZ; ++i) {
            hhdr -> hb_marks[i] &= ~(entry -> marks[i]);
        }
        if (EXPECT(hhdr -> page_reclaim_state == IN_FLOATING
           && hhdr -> hb_n_marks <= full_thres, 0))
        {
            hhdr -> page_reclaim_state = IN_RECLAIMLIST;
            struct hblk* top_hb = *rlh;
            hhdr -> hb_next = top_hb;
            hhdr -> hb_prev = NULL;
            if (top_hb != NULL){
                hdr* top_hhdr = MAK_get_hdr_and_update_hc(
                   top_hb -> hb_body, hc, hc_sz);
                top_hhdr -> hb_prev = h;
            }
            *rlh = h;
        }
    }
    if (!already_unlocked)
    {
        MAK_UNLOCK_GRAN(gran);
    }

    //now go through it and flush it    
    for (entry = start; entry <= end; entry++)
    {
        if (entry -> block == 0) continue;
        hhdr = entry -> hhdr;
        MAK_NVM_ASYNC_RANGE_VIA(&(hhdr -> hb_marks), sizeof (hhdr -> hb_marks),
           aflush_tb, aflush_tb_sz);
        MAK_NVM_ASYNC_RANGE_VIA(&(hhdr -> hb_n_marks), sizeof (hhdr -> hb_n_marks),
           aflush_tb, aflush_tb_sz);
    }
}



/* called without holding the lock from thread local code */
/* tries to do most work without holding the lock. */

MAK_INNER void MAK_truncate_fast_fl(fl_hdr* flh, word ngranules, int k,
                         word keep, word max,
                         hdr_cache_entry* hc, word hc_sz,
                         void** aflush_tb, word aflush_tb_sz)
{
    word trun_tb_sz = 16;
    struct trun_tb_e trun_tb[16] = {0};


    struct hblk** rlh = &(MAK_reclaim_list[k][ngranules]);
    size_t sz = GRANULES_TO_BYTES(ngranules);

    word full_threshold = (word) BLOCK_NEARLY_FULL_THRESHOLD(sz);

    signed_word t_count = flh -> count;
    signed_word k_count = keep < t_count ? (signed_word) keep : t_count;

    if (k_count == t_count){
        return;
    }


    word start = flh -> start_idx;
    signed_word n_truncated = t_count - k_count;


    hdr* hhdr;
    struct hblk* h;
    word bit_no;
    void* p;
    signed_word i;

    void** flp = flh -> fl;
    for (i = 0; i < n_truncated; i++){
        p =  flp[ (start + (word) i) % max ];
        word p_block = (word)(p) >> LOG_HBLKSIZE;
        struct trun_tb_e* entry = trun_tb + (p_block & (((word) trun_tb_sz)-1));
        /* not already in the truncate table */
        if (EXPECT(entry -> block != p_block, 0))
        {
            /* is there an entry in its place already in the */
            /* truncate table? */
            if (EXPECT(entry -> block != 0, 0)){
                remove_trun_tb_entry(entry, entry, ngranules, k,
                    full_threshold, rlh, hc, hc_sz, aflush_tb, aflush_tb_sz);
                BZERO(entry -> marks, sizeof(entry -> marks));
                entry -> mark_count = 0;
            }

            entry -> block = p_block;
            hhdr = MAK_get_hdr_and_update_hc(p, hc, hc_sz);
            entry -> hhdr = hhdr;
        }

        hhdr = entry -> hhdr;
        h = HBLKPTR(p);
        bit_no = MARK_BIT_NO((ptr_t)p - (ptr_t) h, sz);

        set_mark_bit_from_mark_bits(entry -> marks, bit_no);
        entry -> mark_count++;
    }
    remove_trun_tb_entry(&trun_tb[0], &trun_tb[trun_tb_sz - 1],
           ngranules, k, full_threshold, rlh, hc, hc_sz,
           aflush_tb, aflush_tb_sz);

    flh -> count = k_count;
    flh -> start_idx = (start + (word) n_truncated) % max;

}

MAK_INNER MAK_bool MAK_block_empty(hdr *hhdr)
{
    return (hhdr -> hb_n_marks == 0);
}

