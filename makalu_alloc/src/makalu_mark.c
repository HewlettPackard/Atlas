#include "makalu_internal.h"
#include <stdint.h>


MAK_INNER int MAK_all_interior_pointers = MAK_INTERIOR_POINTERS;
STATIC MAK_bool MAK_mark_stack_too_small = FALSE;
STATIC MAK_bool MAK_objects_are_marked = FALSE;
STATIC struct hblk * scan_ptr;
STATIC word MAK_n_rescuing_pages = 0;
                                /* Number of dirty pages we marked from */
                                /* excludes ptrfree pages, etc.         */


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

#define MS_PUSH_UNCOLLECTABLE 2 /* I holds, except that marked          */
                                /* uncollectible objects above scan_ptr */
                                /* may point to unmarked objects.       */
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

MAK_INNER void MAK_push_persistent_roots()
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

STATIC void MAK_invalidate_mark_state(void)
{
    MAK_mark_state = MS_INVALID;
    MAK_mark_stack_top = MAK_mark_stack-1;
}

static void clear_marks_for_block(struct hblk *h, word dummy)
{
    register hdr * hhdr = HDR(h);

    if (IS_UNCOLLECTABLE(hhdr -> hb_obj_kind)) return;
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

/* Perform a small amount of marking.                   */
/* We try to touch roughly a page of memory.            */
/* Return TRUE if we just finished a mark phase.        */
/* Cold_gc_frame is an address inside a GC frame that   */
/* remains valid until all marking is complete.         */
/* A zero value indicates that it's OK to miss some     */
/* register values.                                     */
/* We hold the allocation lock.  In the case of         */
/* incremental collection, the world may not be stopped.*/

STATIC MAK_bool MAK_mark_some(ptr_t cold_gc_frame)
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
                scan_ptr = MAK_push_next_marked(scan_ptr)
                if (scan_ptr == 0) {
                    MAK_push_peristent_roots();
                    MAK_objects_are_marked = TRUE;
                    if (MAK_mark_state != MS_INVALID) {
                        MAK_mark_state = MS_ROOTS_PUSHED;
                    }
                }
            }
            return(FALSE);
        case MS_PUSH_UNCOLLECTABLE:
            if (MAK_mark_stack_top
                >= MAK_mark_stack + MAK_mark_stack_size/4) {
#               ifdef MAK_THREADS
                  /* Avoid this, since we don't parallelize the marker  */
                  /* here.                                              */
                  if (MAK_parallel) MAK_mark_stack_too_small = TRUE;
#               endif
                MARK_FROM_MARK_STACK();
                return(FALSE);
            } else {
                scan_ptr = MAK_push_next_marked_uncollectable(scan_ptr);
                if (scan_ptr == 0) {
                    MAK_push_persistent_roots();
                    MAK_objects_are_marked = TRUE;
                    if (MAK_mark_state != MS_INVALID) {
                        MAK_mark_state = MS_ROOTS_PUSHED;
                    }
                }
            }
            return(FALSE);
        case MS_ROOTS_PUSHED:
#           ifdef PARALLEL_MARK
              /* In the incremental GC case, this currently doesn't     */
              /* quite do the right thing, since it runs to             */
              /* completion.  On the other hand, starting a             */
              /* parallel marker is expensive, so perhaps it is         */
              /* the right thing?                                       */
              /* Eventually, incremental marking should run             */
              /* asynchronously in multiple threads, without grabbing   */
              /* the allocation lock.                                   */
                if (MAK_parallel) {
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
                MAK_mark_state = MS_PUSH_UNCOLLECTABLE;
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
    MAK_n_rescuing_pages = 0;
    if (MAK_mark_state == MS_NONE) {
        MAK_mark_state = MS_PUSH_RESCUERS;
    } else if (MAK_mark_state != MS_INVALID) {
        ABORT("Offline mark: Unexpected state\n");
    } /* else this is really a full collection, and mark        */
      /* bits are invalid.                                      */
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


/* clear all mark bits in the header */
MAK_INNER void MAK_clear_hdr_marks(hdr *hhdr)
{
    size_t last_bit = FINAL_MARK_BIT(hhdr -> hb_sz);
    BZERO(hhdr -> hb_marks, sizeof(hhdr->hb_marks));
    set_mark_bit_from_hdr(hhdr, last_bit);
    hhdr -> hb_n_marks = 0;
}



#ifdef MAK_THREADS

/* Mark using the local mark stack until the global mark stack is empty */
/* and there are no active workers. Update MAK_first_nonempty to reflect */
/* progress.                                                            */
/* Caller does not hold mark lock.                                      */
/* Caller has already incremented MAK_helper_count.  We decrement it,    */
/* and maintain MAK_active_count.                                        */
STATIC void MAK_mark_local(mse *local_mark_stack, int id)
{
    //TODO: to be filled

}

STATIC MAK_bool MAK_help_wanted = FALSE;  /* Protected by mark lock       */
STATIC unsigned MAK_helper_count = 0;    /* Number of running helpers.   */
                                        /* Protected by mark lock       */
//STATIC unsigned MAK_active_count = 0;    /* Number of active helpers.    */
                                        /* Protected by mark lock       */
                                        /* May increase and decrease    */
                                        /* within each mark cycle.  But */
                                        /* once it returns to 0, it     */
                                        /* stays zero for the cycle.    */


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

