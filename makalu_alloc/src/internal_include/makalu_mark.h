/*
 * Source code is partially derived from Boehm-Demers-Weiser Garbage 
 * Collector (BDWGC) version 7.2 (license is attached)
 *
 * File:
 *   gc_pmark.h
 *
 *   Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
 *   Copyright (c) 2001 by Hewlett-Packard Company. All rights reserved.
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
 *
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
#ifndef _MAKALU_MARK_H
#define _MAKALU_MARK_H

MAK_EXTERN int MAK_all_interior_pointers;

MAK_INNER void MAK_init_persistent_roots();
MAK_INNER void MAK_init_object_map(ptr_t start);
MAK_INNER void MAK_initialize_offsets(void);
MAK_INNER void MAK_register_displacement_inner(size_t offset);
MAK_INNER void MAK_mark_init();
MAK_INNER void MAK_clear_hdr_marks(hdr *hhdr);

/* Number of mark stack entries to discard on overflow. */
#define MAK_MARK_STACK_DISCARDS (INITIAL_MARK_STACK_SIZE/8)

#ifdef MAK_THREADS
#   define OR_WORD(addr, bits) AO_or((volatile AO_t *)(addr), (AO_t)(bits))

MAK_INNER void MAK_help_marker(void);

#else // ! MAK_THREADS

#   define OR_WORD(addr, bits) (void)(*(addr) |= (bits))

#endif   //MAK_THREADS 


# define set_mark_bit_from_hdr(hhdr,n) \
              OR_WORD((hhdr)->hb_marks+divWORDSZ(n), (word)1 << modWORDSZ(n))

# define set_mark_bit_from_hdr_unsafe(hhdr, n) \
       { \
           word* addr = (hhdr)->hb_marks+divWORDSZ(n); \
           word bits = (word) 1 << modWORDSZ(n); \
           (*(addr) |= (bits)); \
       }

# define set_mark_bit_from_mark_bits(marks, n) \
       { \
           word* addr = (marks)+divWORDSZ(n); \
           word bits = (word) 1 << modWORDSZ(n); \
           (*(addr) |= (bits)); \
       }

# define mark_bit_from_hdr(hhdr,n) \
              (((hhdr)->hb_marks[divWORDSZ(n)] >> modWORDSZ(n)) & (word)1)

#define  mark_bit_from_mark_bits(marks, n) \
              ((marks[divWORDSZ(n)] >> modWORDSZ(n)) & (word)1)

# define clear_and_flush_mark_bit_from_hdr(hhdr, n, aflush_tb, aflush_tb_sz) \
       { \
           word* addr = (hhdr)->hb_marks+divWORDSZ(n); \
           word bits = ~ ((word) 1 << modWORDSZ(n)); \
           (*(addr) &= (bits)); \
           MAK_NVM_ASYNC_RANGE_VIA(addr, sizeof(word), \
                 aflush_tb, aflush_tb_sz); \
       }

#if defined(POINTER_MASK) && defined(POINTER_SHIFT)
 #define FIXUP_POINTER(p) (p = ((p) & POINTER_MASK) << POINTER_SHIFT)
#endif

#if defined(FIXUP_POINTER)
 #define NEED_FIXUP_POINTER 1
#else
 #define NEED_FIXUP_POINTER 0
 #define FIXUP_POINTER(p)
#endif



/*
 * As above, but interior pointer recognition as for
 * normal heap pointers.
 */
#define MAK_PUSH_ONE_HEAP(p,source) \
    FIXUP_POINTER(p); \
    if ((ptr_t)(p) >= (ptr_t)MAK_least_plausible_heap_addr \
         && (ptr_t)(p) < (ptr_t)MAK_greatest_plausible_heap_addr) { \
      MAK_mark_stack_top = MAK_mark_and_push( \
                            (void *)(p), MAK_mark_stack_top, \
                            MAK_mark_stack_limit, (void * *)(source)); \
    }

# if GRANULE_WORDS == 1
#   define USE_PUSH_MARKED_ACCELERATORS
#   define PUSH_GRANULE(q) \
                { word qcontents = (q)[0]; \
                  MAK_PUSH_ONE_HEAP(qcontents, (q)); }
# elif GRANULE_WORDS == 2
#   define USE_PUSH_MARKED_ACCELERATORS
#   define PUSH_GRANULE(q) \
                { word qcontents = (q)[0]; \
                  MAK_PUSH_ONE_HEAP(qcontents, (q)); \
                  qcontents = (q)[1]; \
                  MAK_PUSH_ONE_HEAP(qcontents, (q)+1); }
# elif GRANULE_WORDS == 4
#   define USE_PUSH_MARKED_ACCELERATORS
#   define PUSH_GRANULE(q) \
                { word qcontents = (q)[0]; \
                  MAK_PUSH_ONE_HEAP(qcontents, (q)); \
                  qcontents = (q)[1]; \
                  MAK_PUSH_ONE_HEAP(qcontents, (q)+1); \
                  qcontents = (q)[2]; \
                  MAK_PUSH_ONE_HEAP(qcontents, (q)+2); \
                  qcontents = (q)[3]; \
                  MAK_PUSH_ONE_HEAP(qcontents, (q)+3); }
# endif


/* Set mark bit, exit if it was already set.    */
#ifdef MAK_THREADS
    /* This is used only if we explicitly set USE_MARK_BITS.            */
    /* The following may fail to exit even if the bit was already set.  */
    /* For our uses, that's benign:                                     */
    //TODO: fix the following code, in pathological case two threads may
    //doing quite a bit of redundant work. Use compare and swap
   #define OR_WORD_EXIT_IF_SET(addr, bits, exit_label) \
        { \
          if (!(*(addr) & (bits))) { \
            AO_or((AO_t *)(addr), (bits)); \
          } else { \
            goto exit_label; \
          } \
        }
#else // ! MAK_THREADS

    #define OR_WORD_EXIT_IF_SET(addr, bits, exit_label) \
        { \
           word old = *(addr); \
           word my_bits = (bits); \
           if (old & my_bits) goto exit_label; \
           *(addr) = (old | my_bits); \
         }
#endif // MAK_THREADS

# define SET_MARK_BIT_EXIT_IF_SET(hhdr,bit_no,exit_label) \
    { \
        word * mark_word_addr = hhdr -> hb_marks + divWORDSZ(bit_no); \
        OR_WORD_EXIT_IF_SET(mark_word_addr, (word)1 << modWORDSZ(bit_no), \
                            exit_label); \
    }

#ifdef MAK_THREADS
  #define INCR_MARKS(hhdr) \
            AO_store(&hhdr->hb_n_marks, AO_load(&hhdr->hb_n_marks) + 1)
#else
  #define INCR_MARKS(hhdr) (void)(++hhdr->hb_n_marks)
#endif


/* Push the object obj with corresponding heap block header hhdr onto   */
/* the mark stack.                                                      */
#define PUSH_OBJ(obj, hhdr, mark_stack_top, mark_stack_limit) \
{ \
    register word _descr = (hhdr) -> hb_descr; \
    MAK_ASSERT(!HBLK_IS_FREE(hhdr)); \
    if (_descr != 0) { \
        mark_stack_top++; \
        if (mark_stack_top >= mark_stack_limit) { \
          mark_stack_top = MAK_signal_mark_stack_overflow(mark_stack_top); \
        } \
        mark_stack_top -> mse_start = (obj); \
        mark_stack_top -> mse_descr = _descr; \
    } \
}



/* If the mark bit corresponding to current is not set, set it, and     */
/* push the contents of the object on the mark stack.  Current points   */
/* to the beginning of the object.  We rely on the fact that the        */
/* preceding header calculation will succeed for a pointer past the     */
/* first page of an object, only if it is in fact a valid pointer       */
/* to the object.  Thus we can omit the otherwise necessary tests       */
/* here.  Note in particular that the "displ" value is the displacement */
/* from the beginning of the heap block, which may itself be in the     */
/* interior of a large object.                                          */
# define PUSH_CONTENTS_HDR(current, mark_stack_top, mark_stack_limit, \
                           source, exit_label, hhdr, do_offset_check) \
{ \
    size_t displ = HBLKDISPL(current); /* Displacement in block; in bytes. */\
    /* displ is always within range.  If current doesn't point to       */ \
    /* first block, then we are in the all_interior_pointers case, and  */ \
    /* it is safe to use any displacement value.                        */ \
    size_t gran_displ = BYTES_TO_GRANULES(displ); \
    size_t gran_offset = hhdr -> hb_map[gran_displ]; \
    size_t byte_offset = displ & (GRANULE_BYTES - 1); \
    ptr_t base = current; \
    /* The following always fails for large block references. */ \
    if (EXPECT((gran_offset | byte_offset) != 0, FALSE))  { \
        if (hhdr -> hb_large_block) { \
          /* gran_offset is bogus.      */ \
          size_t obj_displ; \
          base = (ptr_t)(hhdr -> hb_block); \
          obj_displ = (ptr_t)(current) - base; \
          if (obj_displ != displ) { \
            MAK_ASSERT(obj_displ < hhdr -> hb_sz); \
            /* Must be in all_interior_pointer case, not first block */ \
            /* already did validity check on cache miss.             */ \
          } else { \
            if (do_offset_check && !MAK_valid_offsets[obj_displ]) { \
              goto exit_label; \
            } \
          } \
          gran_displ = 0; \
          MAK_ASSERT(hhdr -> hb_sz > HBLKSIZE || \
                    hhdr -> hb_block == HBLKPTR(current)); \
          MAK_ASSERT((ptr_t)(hhdr -> hb_block) <= (ptr_t) current); \
        } else { \
          size_t obj_displ = GRANULES_TO_BYTES(gran_offset) \
                             + byte_offset; \
          if (do_offset_check && !MAK_valid_offsets[obj_displ]) { \
            goto exit_label; \
          } \
          gran_displ -= gran_offset; \
          base -= obj_displ; \
        } \
    } \
    MAK_ASSERT(hhdr == MAK_find_header(base)); \
    MAK_ASSERT(gran_displ % BYTES_TO_GRANULES(hhdr -> hb_sz) == 0); \
    SET_MARK_BIT_EXIT_IF_SET(hhdr, gran_displ, exit_label); \
    INCR_MARKS(hhdr); \
    PUSH_OBJ(base, hhdr, mark_stack_top, mark_stack_limit); \
}

/* Push the contents of current onto the mark stack if it is a valid    */
/* ptr to a currently unmarked object.  Mark it.                        */
/* If we assumed a standard-conforming compiler, we could probably      */
/* generate the exit_label transparently.                               */
#define PUSH_CONTENTS(current, mark_stack_top, mark_stack_limit, \
                      source, exit_label) \
{ \
    hdr * my_hhdr; \
    HC_GET_HDR(current, my_hhdr, source, exit_label); \
    PUSH_CONTENTS_HDR(current, mark_stack_top, mark_stack_limit, \
                  source, exit_label, my_hhdr, TRUE); \
exit_label: ; \
}


#define MARK_FROM_MARK_STACK() \
   MAK_mark_stack_top = MAK_mark_from(MAK_mark_stack_top, \
        MAK_mark_stack, \
        MAK_mark_stack + MAK_mark_stack_size);


#endif // _MAKALU_MARK_H
