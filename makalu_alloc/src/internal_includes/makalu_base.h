#ifndef _MAKALU_BASE_H
#define _MAKALU_BASE_H

#include "makalu_config.h"


struct hblkhdr {
    size_t hb_sz;  /* If in use, size in bytes, of objects in the block. */
                   /* if free, the size in bytes of the whole block      */
                   /* We assume that this is convertible to signed_word  */
                   /* without generating a negative result.  We avoid    */
                   /* generating free blocks larger than that.           */
    word hb_descr;              /* object descriptor for marking.  See  */
                                /* mark.h.                              */

    struct hblk * hb_block;     /* The corresponding block.             */
    short * hb_map;
    unsigned char hb_flags;
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
};

#endif
