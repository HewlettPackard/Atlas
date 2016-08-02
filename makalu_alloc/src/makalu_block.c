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

