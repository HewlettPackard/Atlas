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
 *   gc_priv.h
 *
 *   Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 *   Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
 *   Copyright (c) 1996-1999 by Silicon Graphics.  All rights reserved.
 *   Copyright (c) 1999-2004 Hewlett-Packard Development Company, L.P.
 *
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

typedef struct MAK_fl_hdr {
     void** fl;
     word start_idx;
     signed_word count;
} fl_hdr;

typedef struct MAK_ms_entry {
    ptr_t mse_start;    /* First word of object, word aligned.  */
    word mse_descr;     /* Descriptor; low order two bits are tags,     */
                        /* as described in gc_mark.h.                   */
} mse;


struct _MAK_transient_metadata {
    
    /*transient scratch memory */

    #define MAK_transient_scratch_free_ptr MAK_transient_md._transient_scratch_free_ptr
    ptr_t _transient_scratch_free_ptr;
    
    #define MAK_transient_scratch_end_ptr MAK_transient_md._transient_scratch_end_ptr
    ptr_t _transient_scratch_end_ptr;

    #define MAK_transient_scratch_last_end_ptr MAK_transient_md._scratch_last_end_ptr
    ptr_t _scratch_last_end_ptr;

    /* free list */

    #define MAK_objfreelist MAK_transient_md._objfreelist
    fl_hdr _objfreelist[MAXOBJGRANULES+1];

    #define MAK_aobjfreelist MAK_transient_md._aobjfreelist
    fl_hdr _aobjfreelist[MAXOBJGRANULES+1];


    #define MAK_reclaim_list MAK_transient_md._reclaim_list
    struct hblk** _reclaim_list[MAXOBJKINDS];

    #define MAK_fl_max_count MAK_transient_md._fl_max_count
    word _fl_max_count[MAXOBJGRANULES+1];

    #define MAK_fl_optimal_count MAK_transient_md._fl_optimal_count
    word _fl_optimal_count[MAXOBJGRANULES+1];

    /* block free lists */

    #define MAK_hblkfreelist MAK_transient_md._hblkfreelist
    struct hblk* _hblkfreelist[N_HBLK_FLS+1];

    #define MAK_free_bytes MAK_transient_md._free_bytes
    word _free_bytes[N_HBLK_FLS+1];

    /* heap */

    #define MAK_n_heap_sects MAK_transient_md._n_heap_sects
    word _n_heap_sects;

    # define MAK_heap_sects MAK_transient_md._heap_sects
    HeapSect _heap_sects[MAX_HEAP_SECTS];        /* Heap segments potentially  */ 
    
    /* mark */
    #define MAK_mark_stack MAK_transient_md._mark_stack
    mse *_mark_stack;
     /* Limits of stack for GC_mark routine.  All ranges     */
     /* between GC_mark_stack (incl.) and GC_mark_stack_top  */
     /* (incl.) still need to be marked from.                */
    #define MAK_mark_stack_limit MAK_transient_md._mark_stack_limit
    mse *_mark_stack_limit;

    #define MAK_mark_stack_top MAK_transient_md._mark_stack_top
    mse *volatile _mark_stack_top;
    /* Updated only with mark lock held, but read asynchronously.   */
    
    #define MAK_mark_stack_size MAK_transient_md._mark_stack_size
    size_t _mark_stack_size;


};


MAK_EXTERN struct _MAK_transient_metadata MAK_transient_md;

MAK_INNER ptr_t MAK_transient_scratch_alloc(size_t bytes);

#define GET_MEM(bytes) MAK_get_transient_memory(bytes)
MAK_INNER ptr_t MAK_get_transient_memory(word bytes);





