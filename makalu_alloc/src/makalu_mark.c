#include "makalu_internal.h"


MAK_INNER int MAK_all_interior_pointers = MAK_INTERIOR_POINTERS;
static MAK_bool MAK_mark_stack_too_small = FALSE;

MAK_INNER void MAK_init_persistent_roots()
{
    int res = GET_MEM_PERSISTENT(&(MAK_persistent_roots_start), MAX_PERSISTENT_ROOTS_SPACE);
    if (res != 0)
        ABORT("Could not allocate space for persistent roots!\n");
    BZERO(MAK_persistent_roots_start, MAX_PERSISTENT_ROOTS_SPACE);
    MAK_NVM_ASYNC_RANGE(MAK_persistent_roots_start, MAX_PERSISTENT_ROOTS_SPACE);
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


/* Mark using the local mark stack until the global mark stack is empty */
/* and there are no active workers. Update GC_first_nonempty to reflect */
/* progress.                                                            */
/* Caller does not hold mark lock.                                      */
/* Caller has already incremented GC_helper_count.  We decrement it,    */
/* and maintain GC_active_count.                                        */
STATIC void MAK_mark_local(mse *local_mark_stack, int id)
{
    //TODO: to be filled

}

#ifdef MAK_THREADS
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
    /* MAK_mark_local decrements GC_helper_count. */
}
#endif

