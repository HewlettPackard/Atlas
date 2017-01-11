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
 *
 */

#ifndef _MAKALU_BASE_H
#define _MAKALU_BASE_H

typedef struct {
    ptr_t hs_start;
    size_t hs_bytes;
} HeapSect;


#define MAK_base_md (*(MAK_base_md_ptr))
MAK_EXTERN struct _MAK_base_md* MAK_base_md_ptr;

struct _MAK_base_md {

    /* header metadata*/
    #define MAK_hdr_spaces MAK_base_md._hdr_spaces
    HeapSect _hdr_spaces[MAX_HEAP_SECTS];
 
    #define MAK_n_hdr_spaces MAK_base_md._n_hdr_spaces
    word _n_hdr_spaces;
  
    #define MAK_hdr_free_ptr MAK_base_md._hdr_free_ptr
    ptr_t _hdr_free_ptr;

    #define MAK_hdr_idx_spaces MAK_base_md._hdr_idx_spaces
    HeapSect _hdr_idx_spaces[MAX_HEAP_SECTS];

    #define MAK_n_hdr_idx_spaces MAK_base_md._n_hdr_idx_spaces
    word _n_hdr_idx_spaces;

    #define MAK_hdr_idx_free_ptr MAK_base_md._hdr_idx_free_ptr
    ptr_t _hdr_idx_free_ptr;

    #define MAK_all_bottom_indices MAK_base_md._all_bottom_indices
    bottom_index * _all_bottom_indices;
                                /* Pointer to first (lowest addr) */
                                /* bottom_index.                  */
    
    #define MAK_all_bottom_indices_end MAK_base_md._all_bottom_indices_end
    bottom_index * _all_bottom_indices_end;
                                /* Pointer to last (highest addr) */
                                /* bottom_index.                  */
    #define MAK_hdr_free_list MAK_base_md._hdr_free_list
    hdr* _hdr_free_list;

    /* persistent logs */

    #define MAK_persistent_log_version  MAK_base_md._persistent_log_version    
    unsigned long _persistent_log_version;
    
    #define MAK_persistent_log_start MAK_base_md._persistent_log_start
    ptr_t _persistent_log_start;

    #define MAK_persistent_initialized MAK_base_md._persistent_initialized
    int _persistent_initialized;

    #define MAK_persistent_state MAK_base_md._persistent_state
    char _persistent_state;


    /* heap size */


    #define MAK_heapsize MAK_base_md._heapsize
    word _heapsize;
 

    #define MAK_last_heap_addr MAK_base_md._last_heap_addr
    ptr_t _last_heap_addr;

    #define MAK_last_heap_size MAK_base_md._last_heap_size
    word  _last_heap_size;

    #define MAK_prev_heap_addr MAK_base_md._prev_heap_addr
    ptr_t _prev_heap_addr;

    #define MAK_max_heapsize MAK_base_md._max_heapsize
    word _max_heapsize;

    #define MAK_greatest_plausible_heap_addr MAK_base_md._greatest_plausible_heap_addr
    void* _greatest_plausible_heap_addr;

    #define MAK_least_plausible_heap_addr MAK_base_md._least_plausible_heap_addr
    void* _least_plausible_heap_addr;

    #define MAK_large_free_bytes MAK_base_md._large_free_bytes
    word _large_free_bytes;
    /* Total bytes contained in blocks on large object free */
    /* list.  */

    /* gc */
    #define MAK_mandatory_gc MAK_base_md._mandatory_gc
    int _mandatory_gc;

    #define MAK_obj_map MAK_base_md._obj_map
    short * _obj_map[MAXOBJGRANULES+1];
                       /* If not NULL, then a pointer to a map of valid */
                       /* object addresses.                             */
                       /* _obj_map[sz_in_granules][i] is                */
                       /* i % sz_in_granules.                           */
                       /* This is now used purely to replace a          */
                       /* division in the marker by a table lookup.     */
                       /* _obj_map[0] is used for large objects and     */
                       /* contains all nonzero entries.  This gets us   */
                       /* out of the marker fast path without an extra  */
                       /* test.            */  
 
    #define MAK_valid_offsets MAK_base_md._valid_offsets
    char _valid_offsets[VALID_OFFSET_SZ];

    #define MAK_modws_valid_offsets MAK_base_md._modws_valid_offsets
    char _modws_valid_offsets[sizeof(word)];

    #define MAK_size_map MAK_base_md._size_map
    size_t _size_map[MAXOBJBYTES+1];
 
    /* obj kinds */
    #define MAK_n_kinds MAK_base_md._n_kinds
    unsigned int _n_kinds;

    #define MAK_persistent_roots_start MAK_base_md._persistent_roots_start
    ptr_t _persistent_roots_start;
};


struct hblk {
    char hb_body[HBLKSIZE];
};

struct MAK_fl_hdr;

struct obj_kind {
   struct MAK_fl_hdr* ok_freelist;

   //struct hblk **ok_reclaim_list;
                        /* List headers for lists of blocks waiting to be */
                        /* swept.                                         */
                        /* Indexed by object size in granules.            */
   word ok_descriptor;  /* Descriptor template for objects in this      */
                        /* block.                                       */
   MAK_bool ok_relocate_descr;
                        /* Add object size in bytes to descriptor       */
                        /* template to obtain descriptor.  Otherwise    */
                        /* template is used as is.                      */
   MAK_bool ok_init;   /* Clear objects before putting them on the free list. */
   MAK_bool ok_seen;  /* this object kind has been witnessed. we persist this */
                     /* information so that we can use to initialize reclaim */
                     /*   list transiently at the start */

};

MAK_EXTERN struct obj_kind* MAK_obj_kinds;

#endif


