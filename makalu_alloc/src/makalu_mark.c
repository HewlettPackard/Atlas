#include "makalu_internal.h"


MAK_INNER int MAK_all_interior_pointers = MAK_INTERIOR_POINTERS;

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

