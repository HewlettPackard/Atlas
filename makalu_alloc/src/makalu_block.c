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
 *   alloc.c
 *
 *   Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 *   Copyright (c) 1991-1996 by Xerox Corporation.  All rights reserved.
 *   Copyright (c) 1998 by Silicon Graphics.  All rights reserved.
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
 *
 */

#include "makalu_internal.h"

STATIC word MAK_free_space_divisor = MAK_FREE_SPACE_DIVISOR;


MAK_INNER MAK_bool MAK_expand_hp_inner(word n)
{
    word bytes;

    if (n < MINHINCR) n = MINHINCR;
    bytes = n * HBLKSIZE;
    /* Make sure bytes is a multiple of MAK_page_size */
      {
        word mask = MAK_page_size - 1;
        bytes += mask;
        bytes &= ~mask;
      }

    if (MAK_max_heapsize != 0 && MAK_heapsize + bytes > MAK_max_heapsize) {
        /* Exceeded self-imposed limit */
        return(FALSE);
    }

    if (MAK_persistent_state == PERSISTENT_STATE_NONE)
        MAK_STORE_NVM_SYNC(MAK_persistent_state, PERSISTENT_STATE_INCONSISTENT);
    //this has to be visible by the time the new address becomes visible
    MAK_STORE_NVM_SYNC(MAK_last_heap_size, bytes);
    ////////////////////////////////////////////////////////
    //KUMUD: We assume that that below happens failure atomically for now."
    int res = GET_MEM_PERSISTENT(&(MAK_last_heap_addr), bytes);
    ///////////////////////////////////////////////////////
    if (res  != 0) {
        return(FALSE);
    }
    return MAK_finish_expand_hp_inner();
}


MAK_INNER MAK_bool MAK_finish_expand_hp_inner(){

    if (MAK_last_heap_addr == NULL)
        goto out;

    /* Adjust heap limits generously for blacklisting to work better.   */
    /* MAK_add_to_heap performs minimal adjustment needed for            */
    /* correctness.                                                     */

    /* Number of bytes by which we expect the */
    /* heap to expand soon.                   */
    word expansion_slop = 4*MAXHINCR*HBLKSIZE;



    MAK_START_NVM_ATOMIC;

    MAK_add_to_heap((struct hblk*) MAK_last_heap_addr, MAK_last_heap_size, expansion_slop);
    /* incase we crash, we need to recompute the plausible heap address, */
    /* we use the information below to decide whether the heap is growing up or down */
    /* we need to revert the partial changes to heap structure , incase the program crashes */
    MAK_STORE_NVM_ADDR(&(MAK_prev_heap_addr), MAK_last_heap_addr);
    MAK_STORE_NVM_ADDR(&(MAK_last_heap_addr), NULL);

    MAK_END_NVM_ATOMIC;

out:
    return (TRUE);
}

MAK_INNER MAK_bool MAK_try_expand_hp(word needed_blocks,
       MAK_bool ignore_off_page, MAK_bool retry)
{
    word blocks_to_get;
    NEED_CANCEL_STATE(int cancel_state);

    DISABLE_CANCEL(cancel_state);

    blocks_to_get = MAK_heapsize/(HBLKSIZE*MAK_free_space_divisor)
                        + needed_blocks;
    if (blocks_to_get > MAXHINCR) {
      word slop;

      /* Get the minimum required to make it likely that we can satisfy */
      /* the current request */
      if (ignore_off_page) {
        slop = 4;
      } else {
        slop = 2 * MINHINCR;
        if (slop > needed_blocks) slop = needed_blocks;
      }
      if (needed_blocks + slop > MAXHINCR) {
        blocks_to_get = needed_blocks + slop;
      } else {
        blocks_to_get = MAXHINCR;
      }
    }

    if (!MAK_expand_hp_inner(blocks_to_get)
        && !MAK_expand_hp_inner(needed_blocks)) {
        ABORT("Out of memory, could not expand heap!");
    }
    RESTORE_CANCEL(cancel_state);
    return(TRUE);
}


MAK_INLINE  word MAK_max(word x, word y)
{
    return(x > y? x : y);
}

MAK_INLINE word MAK_min(word x, word y)
{
    return(x < y? x : y);
}


MAK_INNER void MAK_add_to_heap(struct hblk *space, size_t bytes, word expansion_slop)
{
    //hdr * phdr;
    word endp;
    size_t sz = bytes;
    struct hblk *p = space;

    if (MAK_n_heap_sects >= MAX_HEAP_SECTS) {
        ABORT("Too many heap sections: Increase MAXHINCR or MAX_HEAP_SECTS");
    }
    while ((word)p <= HBLKSIZE) {
        /* Can't handle memory near address zero. */
        ++p;
        sz -= HBLKSIZE;
        if (0 == sz) return;
    }
    endp = (word)p + sz;
    if (endp <= (word)p) {
        /* Address wrapped. */
        sz -= HBLKSIZE;
        if (0 == sz) return;
        endp -= HBLKSIZE;
    }
   
    MAK_ASSERT(endp > (word)p && endp == (word)p + sz);

    MAK_heap_sects[MAK_n_heap_sects].hs_start = (ptr_t)p;
    MAK_heap_sects[MAK_n_heap_sects].hs_bytes = sz;
    MAK_n_heap_sects++;

    MAK_newfreehblk(p, sz);
    MAK_STORE_NVM_WORD(&(MAK_heapsize), MAK_heapsize + sz);
   
    void* greatest_addr = MAK_greatest_plausible_heap_addr;
    void* least_addr = MAK_least_plausible_heap_addr;

    if ((MAK_prev_heap_addr == 0 && !((word)space & SIGNB))
        || (MAK_prev_heap_addr != 0 && MAK_prev_heap_addr < (ptr_t)space)) {
        /* Assume the heap is growing up */
        word new_limit = (word)space + bytes + expansion_slop;
        if (new_limit > (word)space) {
          greatest_addr =
            (void *)MAK_max((word) greatest_addr,
                           (word)new_limit);
        }
    } else {
        /* Heap is growing down */
        word new_limit = (word)space - expansion_slop;
        if (new_limit < (word)space) {
          least_addr =
            (void *)MAK_min((word)least_addr,
                           (word)space - expansion_slop);
        }
    }


    if ((ptr_t)p <= (ptr_t)least_addr
        || least_addr == 0) {
         least_addr = (void *)((ptr_t)p - sizeof(word));
                /* Making it a little smaller than necessary prevents   */
                /* us from getting a false hit from the variable        */
                /* itself.  There's some unintentional reflection       */
                /* here.                                                */
    }
    if ((ptr_t)p + bytes >= (ptr_t)greatest_addr) {
        greatest_addr = (void *)endp;
    }
    //no need to log the below; we recompute greatest plausible heap addr correctly
    //(since prev_heap_addr is logged) in the case of a crash
    MAK_NO_LOG_STORE_NVM(MAK_greatest_plausible_heap_addr, greatest_addr);
    MAK_NO_LOG_STORE_NVM(MAK_least_plausible_heap_addr, least_addr);
}



STATIC struct hblk * MAK_free_block_ending_at(struct hblk *h)
{
    struct hblk * p = h - 1;
    hdr * phdr;

    phdr = HDR(p);
    while (0 != phdr && IS_FORWARDING_ADDR_OR_NIL(phdr)) {
        p = FORWARDED_ADDR(p,phdr);
        phdr = HDR(p);
    }
    if (0 != phdr) {
        if(HBLK_IS_FREE(phdr)) {
            return p;
        } else {
            return 0;
        }
    }
    p = MAK_prev_block(h - 1);
    if (0 != p) {
      phdr = HDR(p);
      if (HBLK_IS_FREE(phdr) && (ptr_t)p + phdr -> hb_sz == (ptr_t)h) {
        return p;
      }
    }
    return 0;
}

STATIC int MAK_hblk_fl_from_blocks(word blocks_needed)
{
    if (blocks_needed <= UNIQUE_THRESHOLD) return (int)blocks_needed;
    if (blocks_needed >= HUGE_THRESHOLD) return N_HBLK_FLS;
    return (int)(blocks_needed - UNIQUE_THRESHOLD)/FL_COMPRESSION
                                        + UNIQUE_THRESHOLD;

}

STATIC void MAK_remove_from_fl(hdr *hhdr, int n)
{
    int index;

    MAK_ASSERT(((hhdr -> hb_sz) & (HBLKSIZE-1)) == 0);
#   ifndef USE_MUNMAP
      /* We always need index to maintain free counts.  */
      if (FL_UNKNOWN == n) {
          index = MAK_hblk_fl_from_blocks(divHBLKSZ(hhdr -> hb_sz));
      } else {
          index = n;
      }
#   endif
    if (hhdr -> hb_prev == 0) {
#       ifdef USE_MUNMAP
          if (FL_UNKNOWN == n) {
            index = MAK_hblk_fl_from_blocks(divHBLKSZ(hhdr -> hb_sz));
          } else {
            index = n;
          }
#       endif
        MAK_ASSERT(HDR(MAK_hblkfreelist[index]) == hhdr);
        MAK_hblkfreelist[index] = hhdr -> hb_next;
    } else {
        hdr *phdr;
        phdr = HDR(hhdr -> hb_prev);
        phdr -> hb_next = hhdr -> hb_next;
    }
    MAK_ASSERT(MAK_free_bytes[index] >= hhdr -> hb_sz);
    INCR_FREE_BYTES(index, - (signed_word)(hhdr -> hb_sz));
    if (0 != hhdr -> hb_next) {
        hdr * nhdr;
        MAK_ASSERT(!IS_FORWARDING_ADDR_OR_NIL(NHDR(hhdr)));
        nhdr = HDR(hhdr -> hb_next);
        nhdr -> hb_prev = hhdr -> hb_prev;
    }
    MAK_LOG_NVM_WORD(&(hhdr -> hb_sz), hhdr -> hb_sz);
    MAK_LOG_NVM_CHAR(&(hhdr -> hb_flags), hhdr -> hb_flags);
}

STATIC void MAK_add_to_fl(struct hblk *h, hdr *hhdr)
{
    int index = MAK_hblk_fl_from_blocks(divHBLKSZ(hhdr -> hb_sz));
    struct hblk *second = MAK_hblkfreelist[index];
    hdr * second_hdr;
#   if defined(MAK_ASSERTIONS) 
      struct hblk *next = (struct hblk *)((word)h + hhdr -> hb_sz);
      hdr * nexthdr = HDR(next);
      struct hblk *prev = MAK_free_block_ending_at(h);
      hdr * prevhdr = HDR(prev);
      MAK_ASSERT(nexthdr == 0 || !HBLK_IS_FREE(nexthdr)
                || (signed_word)MAK_heapsize < 0);
                /* In the last case, blocks may be too large to merge. */
      MAK_ASSERT(prev == 0 || !HBLK_IS_FREE(prevhdr)
                || (signed_word)MAK_heapsize < 0);
#   endif
    MAK_ASSERT(((hhdr -> hb_sz) & (HBLKSIZE-1)) == 0);
    MAK_hblkfreelist[index] = h;

    INCR_FREE_BYTES(index, hhdr -> hb_sz);
    MAK_ASSERT(MAK_free_bytes[index] <= MAK_large_free_bytes);

    hhdr -> hb_next = second;
    hhdr -> hb_prev = 0;

    if (0 != second) {
      second_hdr = HDR(second);
      second_hdr -> hb_prev = h;
    }
    //TODO: Do we really need the following? The only need for this is when it comes through
     //get_first_part_of(). Why not set the FREE_BLK flag there?
    MAK_NO_LOG_STORE_NVM(hhdr -> hb_flags, hhdr -> hb_flags | FREE_BLK);
}



MAK_INNER void MAK_newfreehblk(struct hblk *hbp, word size)
{
    struct hblk *next, *prev;
    hdr *hhdr, *prevhdr, *nexthdr;
    MAK_bool coalesced_prev = FALSE;
    next = (struct hblk *)((word)hbp + size);
    nexthdr = HDR(next);
    prev = MAK_free_block_ending_at(hbp);

    /* Coalesce with predecessor, if possible. */
    if (0 != prev) {
        prevhdr = HDR(prev);
        if ((signed_word)(size + prevhdr -> hb_sz) > 0) {
            MAK_remove_from_fl(prevhdr, FL_UNKNOWN);
            MAK_NO_LOG_STORE_NVM(prevhdr -> hb_sz, (prevhdr -> hb_sz) + size);
          
            hbp = prev;
            hhdr = prevhdr;
            coalesced_prev = TRUE;
        }
    }
    /*just so that we don't have to log later multiple times within the transaction*/
    MAK_LOG_NVM_ADDR(&(MAK_hdr_free_ptr), MAK_hdr_free_ptr);

    if (!coalesced_prev) {
        hhdr = MAK_install_header(hbp);
        if (!hhdr) {
            ABORT ("Could not install header while trying to install new acquired block\n");
        }
        MAK_NO_LOG_STORE_NVM(hhdr->hb_sz, size);
        MAK_NO_LOG_STORE_NVM(hhdr->hb_flags, 0);
    }

    /* Coalesce with successor, if possible */
    if(0 != nexthdr && HBLK_IS_FREE(nexthdr) 
     && (signed_word)(hhdr -> hb_sz + nexthdr -> hb_sz) > 0
         /* no overflow */) {
        MAK_remove_from_fl(nexthdr, FL_UNKNOWN);
        MAK_NO_LOG_STORE_NVM(hhdr -> hb_sz, hhdr -> hb_sz + nexthdr -> hb_sz);
        MAK_remove_header(next);
    }

    MAK_STORE_NVM_WORD(&(MAK_large_free_bytes), MAK_large_free_bytes +size);
    MAK_add_to_fl(hbp, hhdr);
}


/*
 * Free a heap block.
 *
 * Coalesce the block with its neighbors if possible.
 *
 * All mark words are assumed to be cleared.
 */

MAK_INNER void MAK_freehblk(struct hblk *hbp)
{
    struct hblk *next, *prev;
    hdr *hhdr, *prevhdr, *nexthdr;
    signed_word size;
    hhdr = HDR(hbp);
    size = hhdr->hb_sz;
    size = HBLKSIZE * OBJ_SZ_TO_BLOCKS(size);
    if (size <= 0)
        ABORT("Deallocating excessively large block.  Too large an allocation?");
        /* Probably possible if we try to allocate more than half the address */
        /* space at once.  If we don't catch it here, strange things happen   */
        /* later.                                                             */

    if (MAK_persistent_state == PERSISTENT_STATE_NONE)
        MAK_STORE_NVM_SYNC(MAK_persistent_state, PERSISTENT_STATE_INCONSISTENT);

    MAK_remove_counts(hbp, (word)size);
    MAK_STORE_NVM_WORD(&(hhdr->hb_sz), size);

    /* Check for duplicate deallocation in the easy case */
    if (HBLK_IS_FREE(hhdr)) {
        ABORT("Duplicate large block deallocation");
    }

    MAK_STORE_NVM_CHAR(&(hhdr -> hb_flags), hhdr -> hb_flags | FREE_BLK);
    next = (struct hblk *)((word)hbp + size);
    nexthdr = HDR(next);
    prev = MAK_free_block_ending_at(hbp);

    /* Coalesce with predecessor, if possible. */
    if (0 != prev) {
        prevhdr = HDR(prev);
        if ((signed_word)(hhdr -> hb_sz + prevhdr -> hb_sz) > 0) {
            if (MAK_run_mode == RESTARTING_OFFLINE){
                MAK_LOG_NVM_WORD(&(prevhdr -> hb_sz), prevhdr -> hb_sz);
            } else {
                MAK_remove_from_fl(prevhdr, FL_UNKNOWN);
            }
            MAK_NO_LOG_STORE_NVM(prevhdr -> hb_sz, prevhdr -> hb_sz + hhdr -> hb_sz);
            MAK_remove_header(hbp);
            hbp = prev;
            hhdr = prevhdr;
        }
    }

    /* Coalesce with successor, if possible */
    if(0 != nexthdr && HBLK_IS_FREE(nexthdr)
       && (signed_word)(hhdr -> hb_sz + nexthdr -> hb_sz) > 0
        /* no overflow */) {
        if (MAK_run_mode == RESTARTING_OFFLINE){
            MAK_LOG_NVM_CHAR(&(nexthdr -> hb_flags), nexthdr -> hb_flags);
            MAK_LOG_NVM_WORD(&(nexthdr -> hb_sz), nexthdr -> hb_sz);
        } else {
            MAK_remove_from_fl(nexthdr, FL_UNKNOWN);
        }
        MAK_NO_LOG_STORE_NVM(hhdr -> hb_sz, hhdr -> hb_sz + nexthdr -> hb_sz);
        MAK_remove_header(next);
    }
    MAK_STORE_NVM_WORD(&(MAK_large_free_bytes), MAK_large_free_bytes + size);
    if (MAK_run_mode != RESTARTING_OFFLINE)
    {
        MAK_add_to_fl(hbp, hhdr);
    }
}


/*
 * Return a pointer to a block starting at h of length bytes.
 * Memory for the block is mapped.
 * Remove the block from its free list, and return the remainder (if any)
 * to its appropriate free list.
 * May fail by returning 0.
 * The header for the returned block must be set up by the caller.
 * If the return value is not 0, then hhdr is the header for it.
 */
STATIC struct hblk * MAK_get_first_part(struct hblk *h, hdr *hhdr,
                                       size_t bytes, int index)
{
    word total_size = hhdr -> hb_sz;
    struct hblk * rest;
    hdr * rest_hdr;

    MAK_ASSERT((total_size & (HBLKSIZE-1)) == 0);
    MAK_remove_from_fl(hhdr, index);
    if (total_size == bytes) return h;
    rest = (struct hblk *)((word)h + bytes);
    rest_hdr = MAK_install_header(rest);
    if (0 == rest_hdr) {
        /* FIXME: This is likely to be very bad news ... */
        ABORT("Header allocation failed: Dropping block.\n");
        return(0);
    }
    MAK_NO_LOG_STORE_NVM(rest_hdr -> hb_sz, total_size - bytes);
    rest_hdr -> hb_flags = 0;
#   ifdef MAK_ASSERTIONS
      /* Mark h not free, to avoid assertion about adjacent free blocks. */
        hhdr -> hb_flags &= ~FREE_BLK;
#   endif
    MAK_add_to_fl(rest, rest_hdr);
    return h;
}

/* Initialize hdr for a block containing the indicated size and         */
/* kind of objects.                                                     */
/* Return FALSE on failure.                                             */

STATIC MAK_bool setup_header(hdr * hhdr, struct hblk *block, size_t byte_sz,
                            int kind, unsigned flags)
{
    word descr;
    size_t granules;

    /* Set size, kind and mark proc fields */
    MAK_NO_LOG_STORE_NVM(hhdr -> hb_sz, byte_sz);
    MAK_NO_LOG_STORE_NVM(hhdr -> hb_obj_kind, (unsigned char)kind);
    MAK_NO_LOG_STORE_NVM(hhdr -> hb_flags, (unsigned char)flags);
    MAK_NO_LOG_STORE_NVM(hhdr -> hb_block, block);
    descr = MAK_obj_kinds[kind].ok_descriptor;
    if (MAK_obj_kinds[kind].ok_relocate_descr) descr += byte_sz;
    MAK_NO_LOG_STORE_NVM(hhdr -> hb_descr, descr);

    MAK_NO_LOG_STORE_NVM(hhdr -> hb_large_block, (unsigned char)(byte_sz > MAXOBJBYTES));
    granules = BYTES_TO_GRANULES(byte_sz);
    size_t index = (hhdr -> hb_large_block? 0 : granules);
    MAK_NO_LOG_STORE_NVM(hhdr -> hb_map, MAK_obj_map[index]);

    MAK_clear_hdr_marks(hhdr);
    return (TRUE);
}

STATIC struct hblk *
MAK_allochblk_nth(size_t sz, int kind, unsigned flags, int n,
                 MAK_bool may_split)
{
    struct hblk *hbp;
    hdr * hhdr;         /* Header corr. to hbp */
                        /* Initialized after loop if hbp !=0    */
                        /* Gcc uninitialized use warning is bogus.      */
    struct hblk *thishbp;
    hdr * thishdr;              /* Header corr. to hbp */
    signed_word size_needed;    /* number of bytes in requested objects */
    signed_word size_avail;     /* bytes available in this block        */

    size_needed = HBLKSIZE * OBJ_SZ_TO_BLOCKS(sz);
    
    if (MAK_persistent_state == PERSISTENT_STATE_NONE)
        MAK_STORE_NVM_SYNC(MAK_persistent_state, PERSISTENT_STATE_INCONSISTENT);

    hbp = MAK_hblkfreelist[n];
    /* search for a big enough block in free list */
    for(; 0 != hbp; hbp = hhdr -> hb_next) {
        hhdr = HDR(hbp);
        size_avail = hhdr->hb_sz;
        if (size_avail < size_needed) continue;
        if (size_avail != size_needed) {
            signed_word next_size;
            if (!may_split) continue;
            /* If the next heap block is obviously better, go on.     */
            /* This prevents us from disassembling a single large block */
            /* to get tiny blocks.                                    */
            thishbp = hhdr -> hb_next;
            if (thishbp != 0) {
                thishdr = HDR(thishbp);
                next_size = (signed_word)(thishdr -> hb_sz);
                if (next_size < size_avail
                  && next_size >= size_needed) {
                    continue;
                }
            }
        }        

        if( size_avail >= size_needed ) {
            MAK_START_NVM_ATOMIC;
            MAK_LOG_NVM_ADDR(&(MAK_hdr_free_ptr), MAK_hdr_free_ptr);
            hbp = MAK_get_first_part(hbp, hhdr, size_needed, n);
            break;
        }
    }

    if (0 == hbp) return 0;

    /* Set up header */
    if (!setup_header(hhdr, hbp, sz, kind, flags)) {
        ABORT("We could not setup header for the block(s) we are trying to allocate. Leaking memory!!!\n");
        return(0); /* ditto */
    }
    MAK_STORE_NVM_WORD(&(MAK_large_free_bytes), MAK_large_free_bytes - size_needed);
    
    MAK_END_NVM_ATOMIC;

    if (!MAK_install_counts(hbp, (word)size_needed)) {
        /* This leaks memory under very rare conditions. */
        ABORT("We could not istall counts for the block(s) we are trying to allocate. Leaking memory!!!\n");
        return(0);
    }

    return( hbp );
}

/*
 * Allocate (and return pointer to) a heap block
 *   for objects of size sz bytes, searching the nth free list.
 *
 * NOTE: We set obj_map field in header correctly.
 *       Caller is responsible for building an object freelist in block.
 *
 * The client is responsible for clearing the block, if necessary.
 */
MAK_INNER struct hblk *
MAK_allochblk(size_t sz, int kind, unsigned flags/* IGNORE_OFF_PAGE or 0 */)
{
    word blocks;
    int start_list;
    int i;
    struct hblk *result;
    int split_limit; /* Highest index of free list whose blocks we      */
                     /* split.                                          */

    MAK_ASSERT((sz & (GRANULE_BYTES - 1)) == 0);
    blocks = OBJ_SZ_TO_BLOCKS(sz);
    if ((signed_word)(blocks * HBLKSIZE) < 0) {
        return 0;
    }
    start_list = MAK_hblk_fl_from_blocks(blocks);
    /* Try for an exact match first. */
    result = MAK_allochblk_nth(sz, kind, flags, start_list, FALSE);

    if (0 != result) return result;
    
    split_limit = N_HBLK_FLS;

    if (start_list < UNIQUE_THRESHOLD) {
       /* No reason to try start_list again, since all blocks are exact  */
       /* matches.                                                       */
        ++start_list;
    }
    for (i = start_list; i <= split_limit; ++i) {
        struct hblk * result = MAK_allochblk_nth(sz, kind, flags, i, TRUE);
        if (0 != result) return result;
    }
    return 0;
}

/* Allocate a new heapblock for small objects of size gran granules.    */
/* Add all of the heapblock's objects to the free list for objects      */
/* of that size.  Set all mark bits if objects are uncollectible.       */
/* Will fail to do anything if we are out of memory.                    */
MAK_INNER void MAK_new_hblk(size_t gran, int kind)
{
    struct hblk *h;       /* the new heap block */
    MAK_bool clear = MAK_obj_kinds[kind].ok_init;

    MAK_STATIC_ASSERT((sizeof (struct hblk)) == HBLKSIZE);

    /* Allocate a new heap block */
    h = MAK_allochblk(GRANULES_TO_BYTES(gran), kind, 0);
    if (h == 0) return;

    /* Build the free list */
    fl_hdr* flh = &(MAK_obj_kinds[kind].ok_freelist[gran]);
    flh -> count = MAK_build_array_fl(h, GRANULES_TO_WORDS(gran), clear,
                                &(MAK_hdr_cache[0]), (word) HDR_CACHE_SIZE,
                                &(MAK_fl_aflush_table[0]), (word) FL_AFLUSH_TABLE_SZ,
                                flh -> fl);

}
