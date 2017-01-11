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
 *   misc.c
 *
 *   Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 *   Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
 *   Copyright (c) 1999-2001 by Hewlett-Packard Company. All rights reserved.
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

MAK_INNER MAK_bool MAK_is_initialized = FALSE;
STATIC ptr_t beginMAKPersistentMd = 0;
static ptr_t endMAKPersistentMd = 0;

STATIC void MAK_init()
{
    if (MAK_is_initialized)
        return;

    MAK_STATIC_ASSERT(sizeof (ptr_t) == sizeof(word));
    MAK_STATIC_ASSERT(sizeof (signed_word) == sizeof(word));
    MAK_STATIC_ASSERT(sizeof (struct hblk) == HBLKSIZE);
    MAK_STATIC_ASSERT((ptr_t)(word)(-1) > (ptr_t)0);
    /* word should be unsigned */
    MAK_STATIC_ASSERT((word)(-1) > (word)0);
    /* Ptr_t comparisons should behave as unsigned comparisons.       */   

    int sz;
    for (sz = 1; sz <= MAXOBJGRANULES; sz++){
        MAK_fl_max_count[sz] = (word) FL_MAX_SIZE / (word) (sz * GRANULE_BYTES);
        MAK_fl_optimal_count[sz] = (word) FL_OPTIMAL_SIZE / (word) (sz * GRANULE_BYTES);
    } 
    
}

STATIC void MAK_init_size_map(void)
{
    //NVRAM visibility taken care of by MAK_base_md
    int i;
    MAK_size_map[0] = 1;
    for (i = 1; 
         i <= GRANULES_TO_BYTES(TINY_FREELISTS-1) - EXTRA_BYTES; 
         i++)
    {
        MAK_size_map[i] = ROUNDED_UP_GRANULES(i);
        MAK_ASSERT(MAK_size_map[i] < TINY_FREELISTS);
    }
}

STATIC void* MAK_CALL MAK_set_persistent_rgn(MAK_persistent_memalign memalign_func)
{
    MAK_persistent_memalign_func = memalign_func;
    
    size_t total_alloc_sz = sizeof (struct _MAK_base_md)
                 + (MAXOBJKINDS * sizeof (struct obj_kind))
                 + sizeof (bottom_index)
                 + (TOP_SZ * sizeof (bottom_index*))
                 + (MAP_LEN * sizeof(short)) * (MAXOBJGRANULES+1);

    void* ret = 0;
    int res =  GET_MEM_PERSISTENT_PTRALIGN(&ret, total_alloc_sz);

    if (res != 0){
         ABORT("Attept to allocate GC metadata failed");
    }
    BZERO(ret, total_alloc_sz);

    beginMAKPersistentMd = ret;
    endMAKPersistentMd = ret + total_alloc_sz;


    ptr_t alloc_start_addr = (ptr_t) ret;
    size_t alloc_sz;

    //allocate _MAK_base_md in persistent memory
    alloc_sz = sizeof (struct _MAK_base_md);

    //initialize MAK_base_md to all zeros
    MAK_base_md_ptr = (struct _MAK_base_md*) alloc_start_addr;

    MAK_greatest_plausible_heap_addr = 0;
    MAK_least_plausible_heap_addr = (void *)ONES;


    MAK_n_kinds = MAK_N_KINDS_INITIAL_VALUE;

    alloc_start_addr += alloc_sz;

    //allocate MAK_obj_kinds
    alloc_sz = MAXOBJKINDS * sizeof (struct obj_kind);
    MAK_obj_kinds = (struct obj_kind*) alloc_start_addr;

    MAK_obj_kinds[PTRFREE] =  (struct obj_kind){ &MAK_aobjfreelist[0], 
      /*0  filled in dynamically ,*/
      0 | MAK_DS_LENGTH, FALSE, FALSE   , 
      TRUE /* ok_seen */}; 

    MAK_obj_kinds[NORMAL] = (struct obj_kind) { &MAK_objfreelist[0], /*0,*/
                0 | MAK_DS_LENGTH,  /* Adjusted in MAK_init for EXTRA_BYTES */
                TRUE /* add length to descr */, TRUE /*DO_INITALIZED_MALLOC*/
                , TRUE /* ok_seen */}; 
   
    if (!MAK_alloc_reclaim_list(NORMAL))
       ABORT("Failed to allocate transient memory for reclaim list\n");
    if (!MAK_alloc_reclaim_list(PTRFREE))
       ABORT("Failed to allocate transient memory for reclaim list\n");


    alloc_start_addr += alloc_sz;
    alloc_sz = sizeof (bottom_index);
    MAK_all_nils = (bottom_index*) alloc_start_addr;

    alloc_start_addr += alloc_sz;
    alloc_sz = TOP_SZ * sizeof (bottom_index*);
    MAK_top_index = (bottom_index**) alloc_start_addr;

    alloc_start_addr += alloc_sz;
    alloc_sz = (MAP_LEN * sizeof(short)) * (MAXOBJGRANULES+1);
    MAK_init_object_map(alloc_start_addr);

    MAK_initialize_offsets();
    MAK_register_displacement_inner(0L);
    if (!MAK_all_interior_pointers) {
        /* TLS ABI uses pointer-sized offsets for dtv. */
        MAK_register_displacement_inner(sizeof(void *));
    }

    MAK_init_persistent_roots();
    MAK_init_persistent_logs();
    MAK_init_headers();    

    return ret;
}

MAK_API void* MAK_CALL MAK_start(MAK_persistent_memalign funcptr)
{
    void* res = MAK_set_persistent_rgn(funcptr);
    MAK_run_mode = STARTING_ONLINE;
    word initial_heap_sz;
    initial_heap_sz = (word) MINHINCR;
    if(!MAK_expand_hp_inner(initial_heap_sz)) {
        MAK_err_printf("Can't start up: not enough memory\n");
        EXIT();
    }

    /* Adjust normal object descriptor for extra allocation.    */
    if (ALIGNMENT > MAK_DS_TAGS && EXTRA_BYTES != 0) {
        MAK_obj_kinds[NORMAL].ok_descriptor = ((word)(-ALIGNMENT) | MAK_DS_LENGTH);
    }

    MAK_init_size_map();
    MAK_NVM_ASYNC_RANGE(beginMAKPersistentMd, endMAKPersistentMd - beginMAKPersistentMd);
    MAK_sync_all_persistent();
    MAK_STORE_NVM_SYNC(MAK_persistent_initialized, MAGIC_NUMBER);
    MAK_init();
    MAK_thr_init();

    MAK_is_initialized = TRUE;

    return res;
}

STATIC void MAK_reinit_persistent(ptr_t start_addr)
{

    ptr_t curr_addr = start_addr;
    size_t sz;

    MAK_base_md_ptr = (struct _MAK_base_md*) curr_addr;
    sz = sizeof (struct _MAK_base_md);
    curr_addr += sz;

    //assert that MAK_base_md have been properly initialized in the past
    if (MAK_persistent_initialized != MAGIC_NUMBER)
        ABORT("GC metadata is either corupted or has not been properly initialized");

    MAK_obj_kinds = (struct obj_kind*) curr_addr;
    sz = MAXOBJKINDS * sizeof (struct obj_kind);
    curr_addr += sz;

    MAK_all_nils = (bottom_index*) curr_addr;
    sz = sizeof (bottom_index);
    curr_addr += sz;

    MAK_top_index = (bottom_index**) curr_addr;
    sz = TOP_SZ * sizeof (bottom_index*);
    curr_addr += sz;

}


STATIC void MAK_fixup_transient_freelist(){
    MAK_obj_kinds[PTRFREE].ok_freelist = (fl_hdr*) &MAK_aobjfreelist[0];
    MAK_obj_kinds[NORMAL].ok_freelist =  (fl_hdr*) &MAK_objfreelist[0];

    int n_added_kinds = (MAK_n_kinds - MAK_N_KINDS_INITIAL_VALUE);
    if (n_added_kinds <= 0) return;

    word fl_sz = (MAXOBJGRANULES + 1) * sizeof (fl_hdr);

    word alloc_sz = n_added_kinds * fl_sz;
    void* res = MAK_transient_scratch_alloc(alloc_sz);
    if (res == 0)
        ABORT("Failed to allocate transient free list!");
    BZERO(res, alloc_sz);
    int k;
    for (k = MAK_N_KINDS_INITIAL_VALUE; k < MAK_n_kinds; k++){
        MAK_obj_kinds[k].ok_freelist = (fl_hdr*) res;
        res += fl_sz;
   }
}


MAK_API void MAK_CALL MAK_restart(char* start_addr, 
    MAK_persistent_memalign funcptr)
{
    MAK_run_mode = RESTARTING_ONLINE;
    MAK_persistent_memalign_func = funcptr;
    MAK_reinit_persistent(start_addr);

    if (MAK_persistent_state == PERSISTENT_STATE_INCONSISTENT)
        ABORT("GC: Run offline recovery first. Inconsistent state detected!\n");

    //TODO: do we need to be as harsh as below, or should we just print
    //out a warning???
    if (MAK_mandatory_gc)
        ABORT("GC: Mandatory Offline gc required!\n");


    MAK_restart_persistent_scratch_alloc();
    
    int kind;
    for (kind = 0; kind < MAK_n_kinds; kind++) {
        if (MAK_obj_kinds[kind].ok_seen && !MAK_alloc_reclaim_list(kind)) goto out;
    }



    MAK_restart_block_freelists();

    MAK_fixup_transient_freelist();

    MAK_init();
    MAK_thr_init();

    MAK_is_initialized = TRUE;

    return;

out:
    ABORT ("Failed to restart from the persistent metadata");

}

MAK_API void MAK_CALL MAK_start_off(char* start_addr,
    MAK_persistent_memalign funcptr)
{
    MAK_run_mode = RESTARTING_OFFLINE;
    MAK_persistent_memalign_func = funcptr;
    MAK_reinit_persistent(start_addr);

    if (MAK_persistent_state != PERSISTENT_STATE_NONE){
        MAK_RECOVER_METADATA();
    }

    MAK_restart_persistent_scratch_alloc();

    if (MAK_persistent_state != PERSISTENT_STATE_NONE){
        MAK_rebuild_metadata_from_headers();
        /*tests if we crashed in the middle of */
        /* expanding heap, and finish the business */
        MAK_finish_expand_hp_inner();
        MAK_sync_alloc_metadata();
    }
    
    MAK_init();
    MAK_mark_init();
    MAK_start_mark_threads();
    MAK_is_initialized = TRUE;
}

/* takes down the main thread local heap */
/* synchronizes all persistent metadata */
/* for clean restart in next execution cycle */
MAK_API void MAK_CALL MAK_close(void){
   #ifdef MAK_THREADS
    if (MAK_run_mode != RESTARTING_OFFLINE)
        MAK_teardown_main_thread_local();
   #endif
    MAK_sync_gc_metadata();
    MAK_sync_alloc_metadata();
    MAK_is_initialized = FALSE;
    MAK_ACCUMULATE_FLUSH_COUNT();
}
