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
 *   mark_rts.c
 * 
 *   Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 *   Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
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
 *   mark.c
 *
 *   Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 *   Copyright (c) 1991-1995 by Xerox Corporation.  All rights reserved.
 *   Copyright (c) 2000 by Hewlett-Packard Company.  All rights reserved.
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
#include <stdint.h>


MAK_INNER int MAK_all_interior_pointers = MAK_INTERIOR_POINTERS;
STATIC MAK_bool MAK_mark_stack_too_small = FALSE;
STATIC MAK_bool MAK_objects_are_marked = FALSE;
STATIC struct hblk * scan_ptr;

typedef int mark_state_t;       /* Current state of marking, as follows:*/
                                /* Used to remember where we are during */
                                /* concurrent marking.                  */

                                /* We say something is dirty if it was  */
                                /* written since the last time we       */
                                /* retrieved dirty bits.  We say it's   */
                                /* grungy if it was marked dirty in the */
                                /* last set of bits we retrieved.       */

                                /* Invariant I: all roots and marked    */
                                /* objects p are either dirty, or point */
                                /* to objects q that are either marked  */
                                /* or a pointer to q appears in a range */
                                /* on the mark stack.                   */

#define MS_NONE 0               /* No marking in progress. I holds.     */
                                /* Mark stack is empty.                 */

#define MS_PUSH_RESCUERS 1      /* Rescuing objects are currently       */
                                /* being pushed.  I holds, except       */
                                /* that grungy roots may point to       */
                                /* unmarked objects, as may marked      */
                                /* grungy objects above scan_ptr.       */

#define MS_PUSH_ROOTS 2         /* I holds, */
                                /* Roots may point to unmarked objects  */

#define MS_ROOTS_PUSHED 3       /* I holds, mark stack may be nonempty  */

#define MS_PARTIALLY_INVALID 4  /* I may not hold, e.g. because of M.S. */
                                /* overflow.  However marked heap       */
                                /* objects below scan_ptr point to      */
                                /* marked or stacked objects.           */

#define MS_INVALID 5            /* I may not hold.                      */
STATIC mark_state_t MAK_mark_state = MS_NONE;

MAK_INNER void MAK_init_persistent_roots()
{
    int res = GET_MEM_PERSISTENT(&(MAK_persistent_roots_start), MAX_PERSISTENT_ROOTS_SPACE);
    if (res != 0)
        ABORT("Could not allocate space for persistent roots!\n");
    BZERO(MAK_persistent_roots_start, MAX_PERSISTENT_ROOTS_SPACE);
    MAK_NVM_ASYNC_RANGE(MAK_persistent_roots_start, MAX_PERSISTENT_ROOTS_SPACE);
}

STATIC void alloc_mark_stack(size_t n)
{
    mse * new_stack = (mse *)MAK_transient_scratch_alloc(n * sizeof(struct MAK_ms_entry));
    MAK_mark_stack_too_small = FALSE;
    if (MAK_mark_stack_size != 0) {
        if (new_stack != 0) {
///////////////////////////////////////////////////
//          Kumud TODO: right now this transient memory is leaked. In the future
//                      we probably should return to the underlying system
///////////////////////////////////////////
            MAK_mark_stack = new_stack;
            MAK_mark_stack_size = n;
            MAK_mark_stack_limit = new_stack + n;
        } else {
              WARN("Failed to grow mark stack to %lu frames\n",
                            (unsigned long) n);
        }
    } else {
        if (new_stack == 0) {
            ABORT("No space for mark stack\n");
        }
        MAK_mark_stack = new_stack;
        MAK_mark_stack_size = n;
        MAK_mark_stack_limit = new_stack + n;
    }
    MAK_mark_stack_top = MAK_mark_stack-1;
}

MAK_INNER void MAK_mark_init(void)
{
    alloc_mark_stack(INITIAL_MARK_STACK_SIZE);
}

MAK_API void** MAK_CALL MAK_persistent_root_addr(unsigned int id)
{
    intptr_t* result = ((intptr_t*)MAK_persistent_roots_start) + id;
    if ( (ptr_t) result < MAK_persistent_roots_start + MAX_PERSISTENT_ROOTS_SPACE){
        return ((void**) result);
    }
    ABORT("Persistent_root_addr: id points past the last persistent root!\n");
    return NULL;
}

MAK_API void* MAK_CALL MAK_persistent_root(unsigned int id)
{
    intptr_t* root_addr = ((intptr_t*)MAK_persistent_roots_start) + id;
    if ( (ptr_t) root_addr < MAK_persistent_roots_start + MAX_PERSISTENT_ROOTS_SPACE){
        return ((void*)(*root_addr));
    }
    ABORT("Persistent_root: id points past the last persistent root!\n");
    return NULL;
}

MAK_API void MAK_CALL MAK_set_persistent_root(unsigned int id, void* val)
{
    intptr_t* root_addr = ((intptr_t*)MAK_persistent_roots_start) + id;
    if ( (ptr_t) root_addr < MAK_persistent_roots_start + MAX_PERSISTENT_ROOTS_SPACE){
        *root_addr = (intptr_t) val;
        return;
    }
    ABORT("Set_Persistent_Root: id points past the last persistent root!\n");
}

STATIC mse * MAK_signal_mark_stack_overflow(mse *msp)
{
    MAK_mark_state = MS_INVALID;
    MAK_mark_stack_too_small = TRUE;
    return(msp - MAK_MARK_STACK_DISCARDS);
}

/*
 * Push all locations between b and t onto the mark stack.
 * b is the first location to be checked. t is one past the last
 * location to be checked.
 * Should only be used if there is no possibility of mark stack
 * overflow.
 */
STATIC void push_all(ptr_t bottom, ptr_t top)
{
    register word length;

    bottom = (ptr_t)(((word) bottom + ALIGNMENT-1) & ~(ALIGNMENT-1));
    top = (ptr_t)(((word) top) & ~(ALIGNMENT-1));
    if (bottom >= top) return;

    MAK_mark_stack_top++;
    if (MAK_mark_stack_top >= MAK_mark_stack_limit) {
        ABORT("Unexpected mark stack overflow");
    }
    length = top - bottom;
#   if MAK_DS_TAGS > ALIGNMENT - 1
        length += MAK_DS_TAGS;
        length &= ~MAK_DS_TAGS;
#   endif
    MAK_mark_stack_top -> mse_start = bottom;
    MAK_mark_stack_top -> mse_descr = length;

}

STATIC void MAK_push_persistent_roots()
{
    intptr_t*  root_end = (intptr_t*) (MAK_persistent_roots_start
                                 + MAX_PERSISTENT_ROOTS_SPACE);
    intptr_t* start = (intptr_t*) MAK_persistent_roots_start;
    intptr_t* end   = start;

    while (start < root_end){
        while (start < root_end){
           if (*start == 0) start++;
           else break;
        }
        end = start;
        while (end < root_end){
           if (*end != 0) end++;
           else break;
        }

        if (end > start){
           push_all((ptr_t) start, (ptr_t) end);
        }
        start = end;
    }
}

STATIC mse * MAK_mark_from(mse *mark_stack_top, mse *mark_stack,
                  mse *mark_stack_limit);


STATIC struct MAK_ms_entry * MAK_mark_and_push(void *obj,
                                                mse *mark_stack_ptr,
                                                mse *mark_stack_limit,
                                                void **src)
{
    hdr * hhdr;

    PREFETCH(obj);
    hhdr = MAK_get_hdr_and_update_hc(obj, MAK_hdr_cache, 
        (unsigned int) HDR_CACHE_SIZE);
    if (EXPECT(IS_FORWARDING_ADDR_OR_NIL(hhdr), FALSE)) {
        if (MAK_all_interior_pointers) {
            MAK_hc_base_with_hdr(obj, MAK_hdr_cache,
             (unsigned int) HDR_CACHE_SIZE, &hhdr);
            if (hhdr == 0) {
              return mark_stack_ptr;
            }
        } else {
            return mark_stack_ptr;
        }
    }
    if (EXPECT(HBLK_IS_FREE(hhdr), FALSE)) {
        return mark_stack_ptr;
    }

    PUSH_CONTENTS_HDR(obj, mark_stack_ptr /* modified */, mark_stack_limit,
                      (ptr_t)src, was_marked, hhdr, TRUE);
 was_marked:
    return mark_stack_ptr;
}


/* Push all objects reachable from marked objects in the given block */
/* containing objects of size 1 granule.                             */
STATIC void MAK_push_marked1(struct hblk *h, hdr *hhdr)
{
    word * mark_word_addr = &(hhdr->hb_marks[0]);
    word *p;
    word *plim;
    word *q;
    word mark_word;

    /* Allow registers to be used for some frequently accessed  */
    /* global variables.  Otherwise aliasing issues are likely  */
    /* to prevent that.                                         */
    ptr_t greatest_ha = MAK_greatest_plausible_heap_addr;
    ptr_t least_ha = MAK_least_plausible_heap_addr;
    mse * mark_stack_top = MAK_mark_stack_top;
    mse * mark_stack_limit = MAK_mark_stack_limit;

#   undef MAK_mark_stack_top
#   undef MAK_mark_stack_limit
#   define MAK_mark_stack_top mark_stack_top
#   define MAK_mark_stack_limit mark_stack_limit
#   undef MAK_greatest_plausible_heap_addr
#   undef MAK_least_plausible_heap_addr
#   define MAK_greatest_plausible_heap_addr greatest_ha
#   define MAK_least_plausible_heap_addr least_ha

    p = (word *)(h->hb_body); 
    plim = (word *)(((word)h) + HBLKSIZE);

    /* go through all words in block */
        while (p < plim) {
            mark_word = *mark_word_addr++;
            q = p;
            while(mark_word != 0) {
              if (mark_word & 1) {
                  PUSH_GRANULE(q);
              } 
              q += GRANULE_WORDS;
              mark_word >>= 1;
            } 
            p += WORDSZ* GRANULE_WORDS;
        }

#   undef MAK_greatest_plausible_heap_addr
#   undef MAK_least_plausible_heap_addr
#   define MAK_greatest_plausible_heap_addr MAK_base_md._greatest_plausible_heap_addr
#   define MAK_least_plausible_heap_addr MAK_base_md._least_plausible_heap_addr
#   undef MAK_mark_stack_top
#   undef MAK_mark_stack_limit
#   define MAK_mark_stack_limit MAK_transient_md._mark_stack_limit
#   define MAK_mark_stack_top MAK_transient_md._mark_stack_top
    MAK_mark_stack_top = mark_stack_top;
}

/* Push all objects reachable from marked objects in the given block */
/* of size 2 (granules) objects.                                     */
STATIC void MAK_push_marked2(struct hblk *h, hdr *hhdr)
{
    word * mark_word_addr = &(hhdr->hb_marks[0]);
    word *p;
    word *plim;
    word *q;
    word mark_word;

    ptr_t greatest_ha = MAK_greatest_plausible_heap_addr;
    ptr_t least_ha = MAK_least_plausible_heap_addr;
    mse * mark_stack_top = MAK_mark_stack_top;
    mse * mark_stack_limit = MAK_mark_stack_limit;

#   undef MAK_mark_stack_top
#   undef MAK_mark_stack_limit
#   define MAK_mark_stack_top mark_stack_top
#   define MAK_mark_stack_limit mark_stack_limit
#   undef MAK_greatest_plausible_heap_addr
#   undef MAK_least_plausible_heap_addr
#   define MAK_greatest_plausible_heap_addr greatest_ha
#   define MAK_least_plausible_heap_addr least_ha

    p = (word *)(h->hb_body);
    plim = (word *)(((word)h) + HBLKSIZE);

    /* go through all words in block */
        while (p < plim) {
            mark_word = *mark_word_addr++;
            q = p;
            while(mark_word != 0) {
              if (mark_word & 1) {
                  PUSH_GRANULE(q);
                  PUSH_GRANULE(q + GRANULE_WORDS);
              }
              q += 2 * GRANULE_WORDS;
              mark_word >>= 2;
            }
            p += WORDSZ*GRANULE_WORDS;
        }

#   undef MAK_greatest_plausible_heap_addr
#   undef MAK_least_plausible_heap_addr
#   define MAK_greatest_plausible_heap_addr MAK_base_md._greatest_plausible_heap_addr
#   define MAK_least_plausible_heap_addr MAK_base_md._least_plausible_heap_addr
#   undef MAK_mark_stack_top
#   undef MAK_mark_stack_limit
#   define MAK_mark_stack_limit MAK_transient_md._mark_stack_limit
#   define MAK_mark_stack_top MAK_transient_md._mark_stack_top
    MAK_mark_stack_top = mark_stack_top;
}

# if GRANULE_WORDS < 4
/* Push all objects reachable from marked objects in the given block */
/* of size 4 (granules) objects.                                     */
/* There is a risk of mark stack overflow here.  But we handle that. */
/* And only unmarked objects get pushed, so it's not very likely.    */
STATIC void MAK_push_marked4(struct hblk *h, hdr *hhdr)
{
    word * mark_word_addr = &(hhdr->hb_marks[0]);
    word *p;
    word *plim;
    word *q;
    word mark_word;

    ptr_t greatest_ha = MAK_greatest_plausible_heap_addr;
    ptr_t least_ha = MAK_least_plausible_heap_addr;
    mse * mark_stack_top = MAK_mark_stack_top;
    mse * mark_stack_limit = MAK_mark_stack_limit;

#   undef MAK_mark_stack_top
#   undef MAK_mark_stack_limit
#   define MAK_mark_stack_top mark_stack_top
#   define MAK_mark_stack_limit mark_stack_limit
#   undef MAK_greatest_plausible_heap_addr
#   undef MAK_least_plausible_heap_addr
#   define MAK_greatest_plausible_heap_addr greatest_ha
#   define MAK_least_plausible_heap_addr least_ha

    p = (word *)(h->hb_body);
    plim = (word *)(((word)h) + HBLKSIZE);

    /* go through all words in block */
        while (p < plim) {
            mark_word = *mark_word_addr++;
            q = p;
            while(mark_word != 0) {
              if (mark_word & 1) {
                  PUSH_GRANULE(q);
                  PUSH_GRANULE(q + GRANULE_WORDS);
                  PUSH_GRANULE(q + 2*GRANULE_WORDS);
                  PUSH_GRANULE(q + 3*GRANULE_WORDS);
              }
              q += 4 * GRANULE_WORDS;
              mark_word >>= 4;
            }
            p += WORDSZ*GRANULE_WORDS;
        }
#   undef MAK_greatest_plausible_heap_addr
#   undef MAK_least_plausible_heap_addr
#   define MAK_greatest_plausible_heap_addr MAK_base_md._greatest_plausible_heap_addr
#   define MAK_least_plausible_heap_addr MAK_base_md._least_plausible_heap_addr
#   undef MAK_mark_stack_top
#   undef MAK_mark_stack_limit
#   define MAK_mark_stack_limit MAK_transient_md._mark_stack_limit
#   define MAK_mark_stack_top MAK_transient_md._mark_stack_top
    MAK_mark_stack_top = mark_stack_top;
}

#endif /* GRANULE_WORDS < 4 */



/* Push all objects reachable from marked objects in the given block */
STATIC void MAK_push_marked(struct hblk *h, hdr *hhdr)
{
    size_t sz = hhdr -> hb_sz;
    word descr = hhdr -> hb_descr;
    ptr_t p;
    word bit_no;
    ptr_t lim;
    mse * MAK_mark_stack_top_reg;
    mse * mark_stack_limit = MAK_mark_stack_limit;

    /* Some quick shortcuts: */
    if ((0 | MAK_DS_LENGTH) == descr) return;
    if (MAK_block_empty(hhdr)/* nothing marked */) return;
    MAK_objects_are_marked = TRUE;
    if (sz > MAXOBJBYTES) {
        lim = h -> hb_body;
    } else {
        lim = (h + 1)->hb_body - sz;
    }

    switch(BYTES_TO_GRANULES(sz)) {
  #if defined(USE_PUSH_MARKED_ACCELERATORS)  
    case 1:
        MAK_push_marked1(h, hhdr);
        break;
    case 2:
        MAK_push_marked2(h, hhdr);
        break;
   #if GRANULE_WORDS < 4
    case 4:
        MAK_push_marked4(h, hhdr);
        break;
   #endif // GRANULE_WORDS < 4
  #endif // USE_PUSH_MARKED_ACCELERATORS
    default:
        MAK_mark_stack_top_reg = MAK_mark_stack_top;
        for (p = h -> hb_body, bit_no = 0; p <= lim;
           p += sz, bit_no += MARK_BIT_OFFSET(sz)) {
            if (mark_bit_from_hdr(hhdr, bit_no)) {
                /* Mark from fields inside the object */
                PUSH_OBJ(p, hhdr, MAK_mark_stack_top_reg, mark_stack_limit);
            }
        }
        MAK_mark_stack_top = MAK_mark_stack_top_reg;
    }
}



/* Similar to MAK_push_marked, but skip over unallocated blocks  */
/* and return address of next plausible block.                  */
STATIC struct hblk * MAK_push_next_marked(struct hblk *h)
{
    hdr * hhdr = HDR(h);

    if (EXPECT(IS_FORWARDING_ADDR_OR_NIL(hhdr) || HBLK_IS_FREE(hhdr), FALSE)) {
      h = MAK_next_used_block(h);
      if (h == 0) return(0);
      hhdr = MAK_find_header((ptr_t)h);
    }
    MAK_push_marked(h, hhdr);
    return(h + OBJ_SZ_TO_BLOCKS(hhdr -> hb_sz));
}


STATIC void MAK_invalidate_mark_state(void)
{
    MAK_mark_state = MS_INVALID;
    MAK_mark_stack_top = MAK_mark_stack-1;
}

static void clear_marks_for_block(struct hblk *h, word dummy)
{
    register hdr * hhdr = HDR(h);

    /* Mark bit for these is cleared only once the object is        */
    /* explicitly deallocated.  This either frees the block, or     */
    /* the bit is cleared once the object is on the free list.      */
    MAK_clear_hdr_marks(hhdr);
}

/*
 * Clear mark bits in all allocated heap blocks.  This invalidates
 * the marker invariant, and sets MAK_mark_state to reflect this.
 * (This implicitly starts marking to reestablish the invariant.)
 */
STATIC void MAK_clear_marks(void)
{
    MAK_apply_to_all_blocks(clear_marks_for_block, (word)0);
    MAK_objects_are_marked = FALSE;
    MAK_mark_state = MS_INVALID;
    scan_ptr = 0;
}

#ifdef MAK_THREADS
STATIC void MAK_do_parallel_mark(void);
#endif

/* Perform a small amount of marking.                   */
/* We try to touch roughly a page of memory.            */
/* Return TRUE if we just finished a mark phase.        */
/* Cold_gc_frame is an address inside a GC frame that   */
/* remains valid until all marking is complete.         */
/* A zero value indicates that it's OK to miss some     */
/* register values.                                     */
/* We hold the allocation lock.  In the case of         */
/* incremental collection, the world may not be stopped.*/

STATIC MAK_bool MAK_mark_some()
{
    switch(MAK_mark_state) {
        case MS_NONE:
            return(FALSE);

        case MS_PUSH_RESCUERS:
            if (MAK_mark_stack_top
                >= MAK_mark_stack_limit - INITIAL_MARK_STACK_SIZE/2) {
                /* Go ahead and mark, even though that might cause us to */
                /* see more marked dirty objects later on.  Avoid this   */
                /* in the future.                                        */
                MAK_mark_stack_too_small = TRUE;
                MARK_FROM_MARK_STACK();
                return(FALSE);
            } else {
                scan_ptr = MAK_push_next_marked(scan_ptr);
                if (scan_ptr == 0) {
                    MAK_push_persistent_roots();
                    MAK_objects_are_marked = TRUE;
                    if (MAK_mark_state != MS_INVALID) {
                        MAK_mark_state = MS_ROOTS_PUSHED;
                    }
                }
            }
            return(FALSE);
        case MS_PUSH_ROOTS:
            if (MAK_mark_stack_top
                >= MAK_mark_stack + MAK_mark_stack_size/4) {
#               ifdef MAK_THREADS
                  /* Avoid this, since we don't parallelize the marker  */
                  /* here.                                              */
                  if (MAK_parallel_mark) MAK_mark_stack_too_small = TRUE;
#               endif
                MARK_FROM_MARK_STACK();
                return(FALSE);
            } else {
                MAK_push_persistent_roots();
                MAK_objects_are_marked = TRUE;
                if (MAK_mark_state != MS_INVALID) {
                    MAK_mark_state = MS_ROOTS_PUSHED;
                }
            }
            return(FALSE);
        case MS_ROOTS_PUSHED:
#           ifdef MAK_THREADS
              /* In the incremental GC case, this currently doesn't     */
              /* quite do the right thing, since it runs to             */
              /* completion.  On the other hand, starting a             */
              /* parallel marker is expensive, so perhaps it is         */
              /* the right thing?                                       */
              /* Eventually, incremental marking should run             */
              /* asynchronously in multiple threads, without grabbing   */
              /* the allocation lock.                                   */
                if (MAK_parallel_mark) {
                  MAK_do_parallel_mark();
                  MAK_ASSERT(MAK_mark_stack_top < (mse *)MAK_first_nonempty);
                  MAK_mark_stack_top = MAK_mark_stack - 1;
                  if (MAK_mark_stack_too_small) {
                    alloc_mark_stack(2*MAK_mark_stack_size);
                  }
                  if (MAK_mark_state == MS_ROOTS_PUSHED) {
                    MAK_mark_state = MS_NONE;
                    return(TRUE);
                  } else {
                    return(FALSE);
                  }
                }
#           endif
            if (MAK_mark_stack_top >= MAK_mark_stack) {
                MARK_FROM_MARK_STACK();
                return(FALSE);
            } else {
                MAK_mark_state = MS_NONE;
                if (MAK_mark_stack_too_small) {
                    alloc_mark_stack(2*MAK_mark_stack_size);
                }
                return(TRUE);
            }
        case MS_INVALID:
        case MS_PARTIALLY_INVALID:
            if (!MAK_objects_are_marked) {
                MAK_mark_state = MS_PUSH_ROOTS;
                return(FALSE);
            }
            if (MAK_mark_stack_top >= MAK_mark_stack) {
                MARK_FROM_MARK_STACK();
                return(FALSE);
            }
            if (scan_ptr == 0 && MAK_mark_state == MS_INVALID) {
                /* About to start a heap scan for marked objects. */
                /* Mark stack is empty.  OK to reallocate.        */
                if (MAK_mark_stack_too_small) {
                    alloc_mark_stack(2*MAK_mark_stack_size);
                }
                MAK_mark_state = MS_PARTIALLY_INVALID;
            }
            scan_ptr = MAK_push_next_marked(scan_ptr);
            if (scan_ptr == 0 && MAK_mark_state == MS_PARTIALLY_INVALID) {
                MAK_push_persistent_roots();
                MAK_objects_are_marked = TRUE;
                if (MAK_mark_state != MS_INVALID) {
                    MAK_mark_state = MS_ROOTS_PUSHED;
                }
            }
            return(FALSE);
        default:
            ABORT("MAK_mark_some: bad state");
            return(FALSE);
    }
}




STATIC MAK_bool MAK_mark_offline(void)
{
    if (MAK_mark_state == MS_NONE) {
        MAK_mark_state = MS_PUSH_RESCUERS;
    } else if (MAK_mark_state != MS_INVALID) {
        WARN("Makalu: offline mark in unexpected state\n", 0);
        return (FALSE);
    } /* mark bits are invalid. */
    scan_ptr = 0;

    while (TRUE){
        if (MAK_mark_some()) break;
    }

    return (TRUE);

}

MAK_API int MAK_CALL MAK_collect_off(void)
{
    MAK_bool result;
    NEED_CANCEL_STATE(int cancel_state;) 

    if(!MAK_is_initialized)
        ABORT("Makalu not properly initialized!\n");

    MAK_LOCK();

    DISABLE_CANCEL(cancel_state);

    MAK_invalidate_mark_state();  /* Clear mark stack.   */
    /* we may re-start after an incomplete mark phase */

    if (!MAK_mandatory_gc)
    {
        /* beyond this point mark bits can be in inconsistent states */
        /* which means that the reclaim list can be in inconsistent state */
        /* we flush the mark bits asynchronously and if we fail anywhere within */
        /* the gc phase, we run the garbage collection again */
        MAK_STORE_NVM_SYNC(MAK_mandatory_gc, TRUE);
    }

    MAK_clear_marks();

    if(!(result = MAK_mark_offline())){
        MAK_invalidate_mark_state();

        return (0);
    } 

    MAK_apply_to_all_blocks(MAK_defer_reclaim_block, 0);
    MAK_sync_gc_metadata();
    RESTORE_CANCEL(cancel_state);
    MAK_UNLOCK();

    return (1);
    
    
}

MAK_INNER void MAK_init_object_map(ptr_t start)
{
    //visibiliy guarantees provided by persistentMd flushing
    size_t len = MAP_LEN * sizeof(short);
    size_t granule = 0;
    short * new_map = (short*) start;
    unsigned displ;
    for (displ = 0; displ < BYTES_TO_GRANULES(HBLKSIZE); displ++) {
        new_map[displ] = 1;  /* Nonzero to get us out of marker fast path. */
    }
    MAK_obj_map[granule] =  new_map;

    for (granule = 1; granule < MAXOBJGRANULES + 1; granule++){
        start += len;
        new_map = (short*) start;
        for (displ = 0; displ < BYTES_TO_GRANULES(HBLKSIZE); displ++) {
            new_map[displ] = (short)(displ % granule);
        }
        MAK_obj_map[granule] = new_map;
    }
}

MAK_INNER void MAK_initialize_offsets(void)
{
  //visibility of below taken care by the flushing of the MAK_base_md
    unsigned i;
    if (MAK_all_interior_pointers) {
    for (i = 0; i < VALID_OFFSET_SZ; ++i)
        MAK_valid_offsets[i] = TRUE;
    } else {
        BZERO(MAK_valid_offsets, sizeof(MAK_valid_offsets));
        for (i = 0; i < sizeof(word); ++i)
            MAK_modws_valid_offsets[i] = FALSE;
    }
}


MAK_INNER void MAK_register_displacement_inner(size_t offset)
{
    if (offset >= VALID_OFFSET_SZ) {
        ABORT("Bad argument to MAK_register_displacement");
    }
    if (!MAK_valid_offsets[offset]) {
      MAK_valid_offsets[offset] = TRUE;
      MAK_modws_valid_offsets[offset % sizeof(word)] = TRUE;
    }
}


/* clear all mark bits in the header */
MAK_INNER void MAK_clear_hdr_marks(hdr *hhdr)
{
    size_t last_bit = FINAL_MARK_BIT(hhdr -> hb_sz);
    BZERO(hhdr -> hb_marks, sizeof(hhdr->hb_marks));
    set_mark_bit_from_hdr(hhdr, last_bit);
    hhdr -> hb_n_marks = 0;
}

/*
 * Mark objects pointed to by the regions described by
 * mark stack entries between mark_stack and mark_stack_top,
 * inclusive.  Assumes the upper limit of a mark stack entry
 * is never 0.  A mark stack entry never has size 0.
 * We try to traverse on the order of a hblk of memory before we return.
 * Caller is responsible for calling this until the mark stack is empty.
 * Note that this is the most performance critical routine in the
 * collector.  Hence it contains all sorts of ugly hacks to speed
 * things up.  In particular, we avoid procedure calls on the common
 * path, we take advantage of peculiarities of the mark descriptor
 * encoding, we optionally maintain a cache for the block address to
 * header mapping, we prefetch when an object is "grayed", etc.
 */
STATIC mse * MAK_mark_from(mse *mark_stack_top, mse *mark_stack,
                            mse *mark_stack_limit)
{
    signed_word credit = HBLKSIZE;  /* Remaining credit for marking work  */
    ptr_t current_p;      /* Pointer to current candidate ptr.    */
    word current; /* Candidate pointer.                   */
    ptr_t limit;  /* (Incl) limit of current candidate    */
                                /* range                                */
    word descr;
    ptr_t greatest_ha = MAK_greatest_plausible_heap_addr;
    ptr_t least_ha = MAK_least_plausible_heap_addr;
    DECLARE_HDR_CACHE;

    # define SPLIT_RANGE_WORDS 128  /* Must be power of 2. */
 
    MAK_objects_are_marked = TRUE;

    INIT_HDR_CACHE;

    while ((((ptr_t)mark_stack_top - (ptr_t)mark_stack) | credit) >= 0)
    {
        current_p = mark_stack_top -> mse_start;
        descr = mark_stack_top -> mse_descr;
  retry:
        /* current_p and descr describe the current object.         */
        /* *mark_stack_top is vacant.                               */
        /* The following is 0 only for small objects described by a simple  */
        /* length descriptor.  For many applications this is the common     */
        /* case, so we try to detect it quickly.                            */
        if (descr & ((~(WORDS_TO_BYTES(SPLIT_RANGE_WORDS) - 1)) | MAK_DS_TAGS)) {
            word tag = descr & MAK_DS_TAGS;
            switch(tag) {
            
            case MAK_DS_LENGTH:
                /* Large length.                                              */
                /* Process part of the range to avoid pushing too much on the */
                /* stack.                                                     */
                MAK_ASSERT(descr < (word)MAK_greatest_plausible_heap_addr
                            - (word)MAK_least_plausible_heap_addr);
              #ifdef MAK_THREADS
                #define SHARE_BYTES 2048
                if (descr > SHARE_BYTES && MAK_parallel_mark
                  && mark_stack_top < mark_stack_limit - 1) {
                    int new_size = (descr/2) & ~(sizeof(word)-1);
                    mark_stack_top -> mse_start = current_p;
                    mark_stack_top -> mse_descr = new_size + sizeof(word);
                                        /* makes sure we handle         */
                                        /* misaligned pointers.         */
                    mark_stack_top++;
                    current_p += new_size;
                    descr -= new_size;
                    goto retry;
                }
              #endif /*MAK_THREADS */
                mark_stack_top -> mse_start =
                 limit = current_p + WORDS_TO_BYTES(SPLIT_RANGE_WORDS-1);
                mark_stack_top -> mse_descr =
                  descr - WORDS_TO_BYTES(SPLIT_RANGE_WORDS-1); 
                /* Make sure that pointers overlapping the two ranges are     */
                /* considered.                                                */
                limit += sizeof(word) - ALIGNMENT;
                break;
            case MAK_DS_BITMAP:
                mark_stack_top--;
                descr &= ~MAK_DS_TAGS;
                credit -= WORDS_TO_BYTES(WORDSZ/2); /* guess */
                while (descr != 0) {
                    if ((signed_word)descr < 0) {
                        current = *(word *)current_p;
                        FIXUP_POINTER(current);
                        if ((ptr_t)current >= least_ha 
                          && (ptr_t)current < greatest_ha) {
                            PREFETCH((ptr_t)current);
                            PUSH_CONTENTS((ptr_t)current, mark_stack_top,
                              mark_stack_limit, current_p, exit1);
                        }
                    }
                    descr <<= 1;
                    current_p += sizeof(word);
                }
                continue;
            case MAK_DS_PROC:
                ABORT("Makalu: user mark procedure currently not supported!\n");
                //mark_stack_top--;
                //credit -= MAK_PROC_BYTES;
                //mark_stack_top =
                //   (*PROC(descr)) ((word *)current_p, mark_stack_top,
                //      mark_stack_limit, ENV(descr));
                //continue;
            case MAK_DS_PER_OBJECT:
                if ((signed_word)descr >= 0) {
                /* Descriptor is in the object.     */
                    descr = *(word *)(current_p + descr - MAK_DS_PER_OBJECT);
                } else {
                /* Descriptor is in type descriptor pointed to by first     */
                /* word in object.                                          */
                    ptr_t type_descr = *(ptr_t *)current_p;
                    /* type_descr is either a valid pointer to the descriptor   */
                    /* structure, or this object was on a free list.  If it     */
                    /* it was anything but the last object on the free list,    */
                    /* we will misinterpret the next object on the free list as */
                    /* the type descriptor, and get a 0 GC descriptor, which    */
                    /* is ideal.  Unfortunately, we need to check for the last  */
                    /* object case explicitly.                                  */
                    if (0 == type_descr) {
                    /* Rarely executed.     */
                        mark_stack_top--;
                        continue;
                    }
                    descr = *(word *)(type_descr
                       - (descr + (MAK_INDIR_PER_OBJ_BIAS
                                - MAK_DS_PER_OBJECT)));
                }
                if (0 == descr) {
                /* Can happen either because we generated a 0 descriptor  */
                /* or we saw a pointer to a free object.          */
                    mark_stack_top--;
                    continue;
                }   
                goto retry;
            default:
            /* Can't happen. */
                limit = 0; /* initialized to prevent warning. */
            } //switch statement

        } else /* Small object with length descriptor */ {
            mark_stack_top--;
            if (descr < sizeof(word)) continue;        
            limit = current_p + (word)descr;
        }
        /* The simple case in which we're scanning a range. */
        MAK_ASSERT(!((word)current_p & (ALIGNMENT-1)));
        credit -= limit - current_p;
        limit -= sizeof(word);
        {
            #define PREF_DIST 4
            word deferred;
            /* Try to prefetch the next pointer to be examined ASAP.        */
            /* Empirically, this also seems to help slightly without        */
            /* prefetches, at least on linux/X86.  Presumably this loop     */
            /* ends up with less register pressure, and gcc thus ends up    */
            /* generating slightly better code.  Overall gcc code quality   */
            /* for this loop is still not great.                            */
        
            for(;;) {
                PREFETCH(limit - PREF_DIST*CACHE_LINE_SZ);
                MAK_ASSERT(limit >= current_p);
                deferred = *(word *)limit;
                FIXUP_POINTER(deferred);
                limit -= ALIGNMENT;
                if ((ptr_t)deferred >= least_ha && (ptr_t)deferred <  greatest_ha) {
                    PREFETCH((ptr_t)deferred);
                    break; 
                }
                if (current_p > limit) goto next_object;
                /* Unroll once, so we don't do too many of the prefetches     */
                /* based on limit.                                            */
                deferred = *(word *)limit;
                FIXUP_POINTER(deferred);
                limit -= ALIGNMENT;
                if ((ptr_t)deferred >= least_ha
                  && (ptr_t)deferred <  greatest_ha) {
                    PREFETCH((ptr_t)deferred);
                    break;
                }
                if (current_p > limit) goto next_object;
            }
            
            while (current_p <= limit) {
            /* Empirically, unrolling this loop doesn't help a lot. */
            /* Since PUSH_CONTENTS expands to a lot of code,        */
            /* we don't.                                            */
                current = *(word *)current_p;
                FIXUP_POINTER(current);
                PREFETCH(current_p + PREF_DIST*CACHE_LINE_SZ);
                if ((ptr_t)current >= least_ha 
                  && (ptr_t)current <  greatest_ha) {
                  /* Prefetch the contents of the */
                  /* object we just pushed.  It's  */
                  /* likely we will need them soon.                             */
                    PREFETCH((ptr_t)current);
                    PUSH_CONTENTS((ptr_t)current, mark_stack_top,
                           mark_stack_limit, current_p, exit2);
                }
                current_p += ALIGNMENT;
            }
            PUSH_CONTENTS((ptr_t)deferred, mark_stack_top,
                         mark_stack_limit, current_p, exit4);
            next_object:;
        }
    }              
    return mark_stack_top;
}

#ifdef MAK_THREADS

STATIC MAK_bool MAK_help_wanted = FALSE;  /* Protected by mark lock       */
STATIC unsigned MAK_helper_count = 0;    /* Number of running helpers.   */
                                        /* Protected by mark lock       */
STATIC unsigned MAK_active_count = 0;    /* Number of active helpers.    */
                                        /* Protected by mark lock       */
                                        /* May increase and decrease    */
                                        /* within each mark cycle.  But */
                                        /* once it returns to 0, it     */
                                        /* stays zero for the cycle.    */
STATIC volatile AO_t MAK_first_nonempty = 0;
        /* Lowest entry on mark stack   */
        /* that may be nonempty.        */
        /* Updated only by initiating   */
        /* thread.                      */


#define ENTRIES_TO_GET 5


/* Steal mark stack entries starting at mse low into mark stack local   */
/* until we either steal mse high, or we have max entries.              */
/* Return a pointer to the top of the local mark stack.                 */
/* *next is replaced by a pointer to the next unscanned mark stack      */
/* entry.                                                               */
STATIC mse * MAK_steal_mark_stack(mse * low, mse * high, mse * local,
                                 unsigned max, mse **next)
{
    mse *p;
    mse *top = local - 1;
    unsigned i = 0;

    MAK_ASSERT(high >= low-1 && (word)(high - low + 1) <= MAK_mark_stack_size);
    for (p = low; p <= high && i <= max; ++p) {
        word descr = AO_load((volatile AO_t *) &(p -> mse_descr));
        if (descr != 0) {
            /* Must be ordered after read of descr: */
            AO_store_release_write((volatile AO_t *) &(p -> mse_descr), 0);
            /* More than one thread may get this entry, but that's only */
            /* a minor performance problem.                             */
            ++top;
            top -> mse_descr = descr;
            top -> mse_start = p -> mse_start;
            MAK_ASSERT((top -> mse_descr & MAK_DS_TAGS) != MAK_DS_LENGTH ||
                      top -> mse_descr < (word)MAK_greatest_plausible_heap_addr
                                         - (word)MAK_least_plausible_heap_addr);
            /* If this is a big object, count it as                     */
            /* size/256 + 1 objects.                                    */
            ++i;
            if ((descr & MAK_DS_TAGS) == MAK_DS_LENGTH) i += (int)(descr >> 8);
        }
    }
    *next = p;
    return top;
}

/* Copy back a local mark stack.        */
/* low and high are inclusive bounds.   */
STATIC void MAK_return_mark_stack(mse * low, mse * high)
{
    mse * my_top;
    mse * my_start;
    size_t stack_size;

    if (high < low) return;
    stack_size = high - low + 1;
    MAK_acquire_mark_lock();
    my_top = MAK_mark_stack_top; /* Concurrent modification impossible. */
    my_start = my_top + 1;
    if (my_start - MAK_mark_stack + stack_size > MAK_mark_stack_size) {
      MAK_mark_state = MS_INVALID;
      MAK_mark_stack_too_small = TRUE;
      /* We drop the local mark stack.  We'll fix things later. */
    } else {
      BCOPY(low, my_start, stack_size * sizeof(mse));
      MAK_ASSERT((mse *)AO_load((volatile AO_t *)(&MAK_mark_stack_top))
                == my_top);
      AO_store_release_write((volatile AO_t *)(&MAK_mark_stack_top),
                             (AO_t)(my_top + stack_size));
                /* Ensures visibility of previously written stack contents. */
    }
    MAK_release_mark_lock();
    MAK_notify_all_marker();
}



/* Mark from the local mark stack.              */
/* On return, the local mark stack is empty.    */
/* But this may be achieved by copying the      */
/* local mark stack back into the global one.   */
STATIC void MAK_do_local_mark(mse *local_mark_stack, mse *local_top)
{
    unsigned n;
#   define N_LOCAL_ITERS 1

#   ifdef MAK_ASSERTIONS
      /* Make sure we don't hold mark lock. */
        MAK_acquire_mark_lock();
        MAK_release_mark_lock();
#   endif
    for (;;) {
        for (n = 0; n < N_LOCAL_ITERS; ++n) {
            local_top = MAK_mark_from(local_top, local_mark_stack,
                                     local_mark_stack + LOCAL_MARK_STACK_SIZE);
            if (local_top < local_mark_stack) return;
            if ((word)(local_top - local_mark_stack)
                        >= LOCAL_MARK_STACK_SIZE / 2) {
                MAK_return_mark_stack(local_mark_stack, local_top);
                return;
            }
        }
        if ((mse *)AO_load((volatile AO_t *)(&MAK_mark_stack_top))
            < (mse *)AO_load(&MAK_first_nonempty)
            && MAK_active_count < MAK_helper_count
            && local_top > local_mark_stack + 1) {
            /* Try to share the load, since the main stack is empty,    */
            /* and helper threads are waiting for a refill.             */
            /* The entries near the bottom of the stack are likely      */
            /* to require more work.  Thus we return those, even though */
            /* it's harder.                                             */
            mse * new_bottom = local_mark_stack
                                + (local_top - local_mark_stack)/2;
            MAK_ASSERT(new_bottom > local_mark_stack
                      && new_bottom < local_top);
            MAK_return_mark_stack(local_mark_stack, new_bottom - 1);
            memmove(local_mark_stack, new_bottom,
                    (local_top - new_bottom + 1) * sizeof(mse));
            local_top -= (new_bottom - local_mark_stack);
        }
    }
}


/* Mark using the local mark stack until the global mark stack is empty */
/* and there are no active workers. Update MAK_first_nonempty to reflect */
/* progress.                                                            */
/* Caller does not hold mark lock.                                      */
/* Caller has already incremented MAK_helper_count.  We decrement it,    */
/* and maintain MAK_active_count.                                        */
STATIC void MAK_mark_local(mse *local_mark_stack, int id)
{
    mse * my_first_nonempty;

    MAK_acquire_mark_lock();
    MAK_active_count++;
    my_first_nonempty = (mse *)AO_load(&MAK_first_nonempty);
    MAK_ASSERT((mse *)AO_load(&MAK_first_nonempty) >= MAK_mark_stack &&
              (mse *)AO_load(&MAK_first_nonempty) <=
              (mse *)AO_load((volatile AO_t *)(&MAK_mark_stack_top)) + 1);
    MAK_release_mark_lock();

    for (;;) {
        size_t n_on_stack;
        unsigned n_to_get;
        mse * my_top;
        mse * local_top;
        mse * global_first_nonempty = (mse *)AO_load(&MAK_first_nonempty);

        MAK_ASSERT(my_first_nonempty >= MAK_mark_stack &&
                  my_first_nonempty <=
                  (mse *)AO_load((volatile AO_t *)(&MAK_mark_stack_top))  + 1);
        MAK_ASSERT(global_first_nonempty >= MAK_mark_stack &&
                  global_first_nonempty <=
                  (mse *)AO_load((volatile AO_t *)(&MAK_mark_stack_top))  + 1);
        if (my_first_nonempty < global_first_nonempty) {
            my_first_nonempty = global_first_nonempty;
        } else if (global_first_nonempty < my_first_nonempty) {
            AO_compare_and_swap(&MAK_first_nonempty,
                                (AO_t) global_first_nonempty,
                                (AO_t) my_first_nonempty);
            /* If this fails, we just go ahead, without updating        */
            /* MAK_first_nonempty.                                       */
        }
        /* Perhaps we should also update MAK_first_nonempty, if it */
        /* is less.  But that would require using atomic updates. */
        my_top = (mse *)AO_load_acquire((volatile AO_t *)(&MAK_mark_stack_top));
        n_on_stack = my_top - my_first_nonempty + 1;
        if (0 == n_on_stack) {
            MAK_acquire_mark_lock();
            my_top = MAK_mark_stack_top;
                /* Asynchronous modification impossible here,   */
                /* since we hold mark lock.                     */
            n_on_stack = my_top - my_first_nonempty + 1;
            if (0 == n_on_stack) {
                MAK_active_count--;
                MAK_ASSERT(MAK_active_count <= MAK_helper_count);
                /* Other markers may redeposit objects  */
                /* on the stack.                                */
                if (0 == MAK_active_count) MAK_notify_all_marker();
                while (MAK_active_count > 0
                       && (mse *)AO_load(&MAK_first_nonempty)
                          > MAK_mark_stack_top) {
                    /* We will be notified if either MAK_active_count    */
                    /* reaches zero, or if more objects are pushed on   */
                    /* the global mark stack.                           */
                    MAK_wait_marker();
                }
                if (MAK_active_count == 0 &&
                    (mse *)AO_load(&MAK_first_nonempty) > MAK_mark_stack_top) {
                    MAK_bool need_to_notify = FALSE;
                    /* The above conditions can't be falsified while we */
                    /* hold the mark lock, since neither                */
                    /* MAK_active_count nor MAK_mark_stack_top can        */
                    /* change.  MAK_first_nonempty can only be           */
                    /* incremented asynchronously.  Thus we know that   */
                    /* both conditions actually held simultaneously.    */
                    MAK_helper_count--;
                    if (0 == MAK_helper_count) need_to_notify = TRUE;
                    MAK_release_mark_lock();
                    if (need_to_notify) MAK_notify_all_marker();
                    return;
                }
                /* else there's something on the stack again, or        */
                /* another helper may push something.                   */
                MAK_active_count++;
                MAK_ASSERT(MAK_active_count > 0);
                MAK_release_mark_lock();
                continue;
            } else {
                MAK_release_mark_lock();
            }
        }
        n_to_get = ENTRIES_TO_GET;
        if (n_on_stack < 2 * ENTRIES_TO_GET) n_to_get = 1;
        local_top = MAK_steal_mark_stack(my_first_nonempty, my_top,
                                        local_mark_stack, n_to_get,
                                        &my_first_nonempty);
        MAK_ASSERT(my_first_nonempty >= MAK_mark_stack &&
                  my_first_nonempty <=
                    (mse *)AO_load((volatile AO_t *)(&MAK_mark_stack_top)) + 1);
        MAK_do_local_mark(local_mark_stack, local_top);
    }
}

/* Perform Parallel mark.                       */
/* We hold the GC lock, not the mark lock.      */
/* Currently runs until the mark stack is       */
/* empty.                                       */
STATIC void MAK_do_parallel_mark(void)
{
    mse local_mark_stack[LOCAL_MARK_STACK_SIZE];

    MAK_acquire_mark_lock();
    MAK_ASSERT(I_HOLD_LOCK());
    /* This could be a MAK_ASSERT, but it seems safer to keep it on      */
    /* all the time, especially since it's cheap.                       */
    if (MAK_help_wanted || MAK_active_count != 0 || MAK_helper_count != 0)
        ABORT("Tried to start parallel mark in bad state");
    MAK_first_nonempty = (AO_t)MAK_mark_stack;
    MAK_active_count = 0;
    MAK_helper_count = 1;
    MAK_help_wanted = TRUE;
    MAK_release_mark_lock();
    MAK_notify_all_marker();
        /* Wake up potential helpers.   */
    MAK_mark_local(local_mark_stack, 0);
    MAK_acquire_mark_lock();
    MAK_help_wanted = FALSE;
    /* Done; clean up.  */
    while (MAK_helper_count > 0) MAK_wait_marker();
    /* MAK_helper_count cannot be incremented while MAK_help_wanted == FALSE */
    MAK_release_mark_lock();
    MAK_notify_all_marker();
}



MAK_INNER void MAK_help_marker()
{

    mse local_mark_stack[LOCAL_MARK_STACK_SIZE];
    unsigned my_id;
    if (!MAK_parallel_mark) return;
    MAK_acquire_mark_lock();
    while (!MAK_help_wanted) {
        MAK_wait_marker();
    }

    my_id = MAK_helper_count;
    if (my_id >= (unsigned) MAK_n_markers) {
        /* This test is useful only if original threads can also        */
      /* act as helpers.  Under Linux they can't.                       */

        MAK_release_mark_lock();
        return;
    }
    MAK_helper_count = my_id + 1;
    MAK_release_mark_lock();
    MAK_mark_local(local_mark_stack, my_id);
    /* MAK_mark_local decrements MAK_helper_count. */
}
#endif

