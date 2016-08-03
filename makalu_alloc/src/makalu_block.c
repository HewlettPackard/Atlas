#include "makalu_internal.h"


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
    MAK_LOG_NVM_CHAR((char*)(&(hhdr -> hb_flags)), (char) (hhdr -> hb_flags));
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
