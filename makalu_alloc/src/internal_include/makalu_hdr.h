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
 *   gc_hdrs.h
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
 */

#ifndef _MAKALU_HDR_H
#define _MAKALU_HDR_H


typedef struct hblkhdr {

    word hb_sz;  /* If in use, size in bytes, of objects in the block. */
                   /* if free, the size in bytes of the whole block      */
                   /* We assume that this is convertible to signed_word  */
                   /* without generating a negative result.  We avoid    */
                   /* generating free blocks larger than that.           */
    word hb_descr;              /* object descriptor for marking.  See  */
                                /* mark.h.                              */

    struct hblk * hb_block;     /* The corresponding block.             */
    short * hb_map;      /* Essentially a table of remainders    */
                               /* mod BYTES_TO_GRANULES(hb_sz), except */
                               /* for large blocks.  See GC_obj_map.   */

    unsigned char hb_flags;
    unsigned char hb_obj_kind;
                         /* Kind of objects in the block.  Each kind    */
                         /* identifies a mark procedure and a set of    */
                         /* list headers.  Sometimes called regions.    */

    unsigned char hb_large_block;  
    char page_reclaim_state;
    word dummy_for_cache_alignment[3];
    counter_t hb_n_marks;       /* Number of set mark bits, excluding   */
                                /* the one always set at the end.       */
                                /* Currently it is concurrently         */
                                /* updated and hence only approximate.  */
                                /* But a zero value does guarantee that */
                                /* the block contains no marked         */
                                /* objects.                             */
                                /* Ensuring this property means that we */
                                /* never decrement it to zero during a  */
                                /* collection, and hence the count may  */
                                /* be one too high.  Due to concurrent  */
                                /* updates, an arbitrary number of      */
                                /* increments, but not all of them (!)  */
                                /* may be lost, hence it may in theory  */
                                /* be much too low.                     */
                                /* The count may also be too high if    */
                                /* multiple mark threads mark the       */
                                /* same object due to a race.           */
                                /* Without parallel marking, the count  */
                                /* is accurate.                         */
    word hb_marks[MARK_BITS_SZ];
    struct hblk * hb_next;      /* Link field for hblk free list         */
                                /* and for lists of chunks waiting to be */
                                /* reclaimed.                            */
    struct hblk * hb_prev;      /* Backwards link for free list.        */
} hdr;


typedef struct bi {
    hdr * index[BOTTOM_SZ];
        /*
         * The bottom level index contains one of three kinds of values:
         * 0 means we're not responsible for this block,
         *   or this is a block other than the first one in a free block.
         * 1 < (long)X <= MAX_JUMP means the block starts at least
         *        X * HBLKSIZE bytes before the current address.
         * A valid pointer points to a hdr structure. (The above can't be
         * valid pointers due to the GET_MEM return convention.)
         */
    struct bi * asc_link;       /* All indices are linked in    */
                                /* ascending order...           */
    struct bi * desc_link;      /* ... and in descending order. */
    word key;                   /* high order address bits.     */
    struct bi * hash_link;      /* Hash chain link.             */
    word dummy_for_cache_align[4];
} bottom_index;

MAK_EXTERN bottom_index * MAK_all_nils;
MAK_EXTERN bottom_index** MAK_top_index;



#define HDR_FROM_BI(bi, p) \
                ((bi)->index[((word)(p) >> LOG_HBLKSIZE) & (BOTTOM_SZ - 1)])

  /* Hash function for tree top level */
# define TL_HASH(hi) ((hi) & (TOP_SZ - 1))
  /* Hash function for tree top level */
# define TL_HASH(hi) ((hi) & (TOP_SZ - 1))
  /* Set bottom_indx to point to the bottom index for address p */
# define GET_BI(p, bottom_indx) \
      { \
          register word hi = \
              (word)(p) >> (LOG_BOTTOM_SZ + LOG_HBLKSIZE); \
          register bottom_index * _bi = MAK_top_index[TL_HASH(hi)]; \
          while (_bi -> key != hi && _bi != MAK_all_nils) \
              _bi = _bi -> hash_link; \
          (bottom_indx) = _bi; \
      }
# define GET_HDR_ADDR(p, ha) \
      { \
          register bottom_index * bi; \
          GET_BI(p, bi); \
          (ha) = &(HDR_FROM_BI(bi, p)); \
      }
# define GET_HDR(p, hhdr) { register hdr ** _ha; GET_HDR_ADDR(p, _ha); \
                            (hhdr) = *_ha; }
# define SET_HDR(p, hhdr) { register hdr ** _ha; GET_HDR_ADDR(p, _ha); \
                            MAK_STORE_NVM_PTR_ASYNC(_ha, (hhdr)); }
# define SET_HDR_NO_LOG(p, hhdr) { register hdr ** _ha; GET_HDR_ADDR(p, _ha); \
                            MAK_NO_LOG_STORE_NVM(*_ha, (hhdr)); }
# define HDR(p) MAK_find_header((ptr_t)(p))

/* Get an HBLKSIZE aligned address closer to the beginning of the block */
/* h.  Assumes hhdr == HDR(h) and IS_FORWARDING_ADDR(hhdr).             */
#define FORWARDED_ADDR(h, hhdr) ((struct hblk *)(h) - (size_t)(hhdr))

// caching headers

typedef struct hce {
  word block_addr;    /* right shifted by LOG_HBLKSIZE */
  hdr * hce_hdr;
} hdr_cache_entry;


#define HCE_VALID_FOR(hce,h) ((hce) -> block_addr == \
                                ((word)(h) >> LOG_HBLKSIZE))

#define HCE_HDR(h) ((hce) -> hce_hdr)


#define HCE(h, hdr_cache, hc_sz) hdr_cache + (((word)(h) >> LOG_HBLKSIZE) \
               & (((word) hc_sz)-1))


//the hdr cache below only used for mark phase //////////////////////

#define DECLARE_HDR_CACHE \
        hdr_cache_entry hdr_cache[HDR_CACHE_SIZE]

#define INIT_HDR_CACHE BZERO(hdr_cache, sizeof(hdr_cache))

MAK_INNER hdr * MAK_header_cache_miss(ptr_t p, hdr_cache_entry *hce);
# define HEADER_CACHE_MISS(p, hce, source) MAK_header_cache_miss(p, hce)


/* Set hhdr to the header for p.  Analogous to GET_HDR below,           */
/* except that in the case of large objects, it                         */
/* gets the header for the object beginning, if MAK_all_interior_ptrs    */
/* is set.                                                              */
/* Returns zero if p points to somewhere other than the first page      */
/* of an object, and it is not a valid pointer to the object.           */
#define HC_GET_HDR(p, hhdr, source, exit_label) \
        { \
          hdr_cache_entry * hce = HCE(p, hdr_cache, HDR_CACHE_SIZE); \
          if (EXPECT(HCE_VALID_FOR(hce, p), TRUE)) { \
            hhdr = hce -> hce_hdr; \
          } else { \
            hhdr = HEADER_CACHE_MISS(p, hce, source); \
    if (0 == hhdr) goto exit_label; \
  } \
}

///////////////////////////////////////////////////////////////

/* allocation time header cache */
//TODO: unify the allocation time and gc time hdr cache
extern hdr_cache_entry MAK_hdr_cache[HDR_CACHE_SIZE];

MAK_INNER void MAK_update_hc(ptr_t p, hdr* hhdr, hdr_cache_entry* hc, word hc_sz);
MAK_INNER hdr* MAK_get_hdr_and_update_hc(ptr_t p, hdr_cache_entry* hc, word hc_sz);
MAK_INNER hdr* MAK_get_hdr_no_update(ptr_t p, hdr_cache_entry* hc, word hc_sz);

MAK_INNER void MAK_init_headers(void);
MAK_INNER void MAK_remove_header(struct hblk *h);
MAK_INNER void MAK_restart_persistent_scratch_alloc();
MAK_INNER void MAK_rebuild_metadata_from_headers();
MAK_INNER struct hblkhdr * MAK_install_header(struct hblk *h);
MAK_INNER MAK_bool MAK_install_counts(struct hblk *h, size_t sz/* bytes */);
MAK_INNER void MAK_remove_counts(struct hblk *h, size_t sz/* bytes */);
MAK_INNER void MAK_apply_to_all_blocks(void (*fn)(struct hblk *h, word client_data),
     word client_data);
MAK_INNER struct hblk * MAK_next_used_block(struct hblk *h);
MAK_INNER struct hblk * MAK_prev_block(struct hblk *h);

# define HBLK_IS_FREE(hdr) (((hdr) -> hb_flags & FREE_BLK) != 0)


MAK_INNER hdr * MAK_find_header(ptr_t h);
MAK_INNER void* MAK_hc_base_with_hdr(void *p, 
  hdr_cache_entry *hct, unsigned int hct_sz, hdr** hdr_ret);

#define ENSURE_64_BIT_COPY(dest, src) \
{ \
   asm volatile("" ::: "memory"); \
   __asm__ __volatile__ ("movq %1, %0;\n" \
       :"=r"(dest) \
       :"r" (src) \
       :); \
   asm volatile("" ::: "memory"); \
}

#endif
