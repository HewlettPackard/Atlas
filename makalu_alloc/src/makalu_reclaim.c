#include "makalu_internal.h"

MAK_INNER MAK_bool MAK_alloc_reclaim_list(unsigned k /*kind */)
{
    struct hblk** result;
    result = (struct hblk **) MAK_transient_scratch_alloc(
              (MAXOBJGRANULES+1) * sizeof(struct hblk *));
    if (result == 0) return(FALSE);
    BZERO(result, (MAXOBJGRANULES+1)*sizeof(struct hblk *));
    MAK_reclaim_list[k] = result;
   
    return (TRUE);
}

STATIC MAK_bool MAK_block_nearly_full(hdr *hhdr)
{
    return (hhdr -> hb_n_marks > BLOCK_NEARLY_FULL_THRESHOLD(hhdr -> hb_sz));
}

MAK_INNER void MAK_restart_block_freelists()
{

    signed_word j;
    bottom_index * index_p;
    struct hblk* h;
    hdr* hhdr;
    word n_blocks;
    int fl_index;
    struct hblk* second;
    hdr* second_hdr;
    word sz;
    unsigned obj_kind;
    struct hblk ** rlh;

    for (index_p = MAK_all_bottom_indices; index_p != 0;
         index_p = index_p -> asc_link) {
        for (j = BOTTOM_SZ-1; j >= 0;) {
            if (!IS_FORWARDING_ADDR_OR_NIL(index_p->index[j])) {
                hhdr = (hdr*) (index_p->index[j]);
                h = hhdr -> hb_block;
                sz = hhdr -> hb_sz;
                if (HBLK_IS_FREE(hhdr)) {
                    //build hblk freelist
                    n_blocks = divHBLKSZ(sz);
                    if (n_blocks <= UNIQUE_THRESHOLD)
                        fl_index = n_blocks;
                    else if (n_blocks >= HUGE_THRESHOLD)
                        fl_index = N_HBLK_FLS;
                    else
                        fl_index = (int)(n_blocks - UNIQUE_THRESHOLD)/FL_COMPRESSION
                                        + UNIQUE_THRESHOLD;
                    MAK_free_bytes[fl_index] += sz;
                    second = MAK_hblkfreelist[fl_index];
                    MAK_hblkfreelist[fl_index] = h;
                    hhdr -> hb_next = second;
                    hhdr -> hb_prev = 0;
                    if (second != 0){
                        second_hdr = HDR(second);
                        second_hdr -> hb_prev = h;
                    }
                }
                else {
                   //build reclaim list
                   MAK_update_hc(h -> hb_body, hhdr, MAK_hdr_cache, (word) HDR_CACHE_SIZE);
                   obj_kind = hhdr -> hb_obj_kind;
                   if ( sz <=  MAXOBJBYTES && !MAK_block_nearly_full(hhdr)) {
                       rlh = &(MAK_reclaim_list[obj_kind][BYTES_TO_GRANULES(sz)]);
                       second = *rlh;
                       hhdr -> hb_next =  second;
                       hhdr -> hb_prev = NULL;
                       if (second != NULL){
                           second_hdr = MAK_get_hdr_and_update_hc(second -> hb_body,
                                                      MAK_hdr_cache, (word) HDR_CACHE_SIZE);
                           second_hdr -> hb_prev = h;
                       }
                       hhdr -> page_reclaim_state = IN_RECLAIMLIST;
                       *rlh = h;
                   }
                }
                j--;
             } else if (index_p->index[j] == 0) {  //is nil
                j--;
             } else {   //is forwarding address
                j -= (signed_word)(index_p->index[j]);
             }
         }
     }
}


MAK_INNER signed_word MAK_build_array_fl(struct hblk *h, size_t sz_in_words, MAK_bool clear,
                           hdr_cache_entry* hc, word hc_sz,
                           void** aflush_tb, word aflush_tb_sz,
                           void** list)
{
    word *p;
    word *last_object;            /* points to last object in new hblk    */
    signed_word idx = 0;
    hdr* hhdr = MAK_get_hdr_and_update_hc(h -> hb_body, hc, hc_sz); 
    
    /* Do a few prefetches here, just because its cheap.          */
    /* If we were more serious about it, these should go inside   */
    /* the loops.  But write prefetches usually don't seem to     */
    /* matter much.                                               */
    PREFETCH_FOR_WRITE((ptr_t)h);
    PREFETCH_FOR_WRITE((ptr_t)h + 128);
    PREFETCH_FOR_WRITE((ptr_t)h + 256);
    PREFETCH_FOR_WRITE((ptr_t)h + 378);

    if (clear) BZERO(h, HBLKSIZE);

    p = (word *)(h -> hb_body); /*first object in *h  */
    last_object = (word *)((char *)h + HBLKSIZE);
    last_object -= sz_in_words;
    /* Last place for last object to start */

    /* make a list of all objects in *h with head as last object */
    while (p <= last_object) {
       /* current object's link points to last object */
        list[idx] = (void*) p;
        idx++;
        p += sz_in_words;
    }

    size_t sz = hhdr -> hb_sz;
    unsigned i;
    for (i = 0; i < MARK_BITS_SZ; ++i) {
        hhdr -> hb_marks[i] = ONES;
    }
    hhdr -> hb_n_marks = HBLK_OBJS(sz);
    hhdr -> page_reclaim_state = IN_FLOATING;
    MAK_NVM_ASYNC_RANGE_VIA(&(hhdr -> hb_marks), sizeof (hhdr -> hb_marks),
                           aflush_tb, aflush_tb_sz);
    MAK_NVM_ASYNC_RANGE_VIA(&(hhdr -> hb_n_marks), sizeof (hhdr -> hb_n_marks),
                           aflush_tb, aflush_tb_sz);
    return idx;
}


STATIC signed_word MAK_reclaim_to_array_clear(struct hblk *hbp, hdr *hhdr, size_t sz,
                              void** aflush_tb, word aflush_tb_sz,
                              void** list)
{

    word bit_no = 0;
    word *p, *q, *plim;
    signed_word n_bytes_found = 0;
    signed_word idx = 0;

    //MAK_ASSERT(hhdr == MAK_find_header((ptr_t)hbp));
    MAK_ASSERT(sz == hhdr -> hb_sz);
    MAK_ASSERT((sz & (BYTES_PER_WORD-1)) == 0);
    p = (word *)(hbp->hb_body);
    plim = (word *)(hbp->hb_body + HBLKSIZE - sz);

    /* go through all words in block */
    while (p <= plim) {
        if( mark_bit_from_hdr(hhdr, bit_no) ) {
            p = (word *)((ptr_t)p + sz);
        } else {
            n_bytes_found += sz;
          /* object is available - put on list */
            list[idx] = p;
            idx++;
          /* Clear object, advance p to next object in the process */
            q = (word *)((ptr_t)p + sz);
            while (p < q) {
              *p++ = 0;
            }
            set_mark_bit_from_hdr_unsafe(hhdr, bit_no);
        }
        bit_no += MARK_BIT_OFFSET(sz);
    }  
    hhdr -> hb_n_marks = HBLK_OBJS(sz);
    hhdr -> page_reclaim_state = IN_FLOATING;    
    if (n_bytes_found != 0){
        MAK_NVM_ASYNC_RANGE_VIA(&(hhdr -> hb_n_marks), 
           sizeof(hhdr -> hb_n_marks), aflush_tb, aflush_tb_sz);
        MAK_NVM_ASYNC_RANGE_VIA(&(hhdr -> hb_marks), 
           sizeof(hhdr -> hb_marks), aflush_tb, aflush_tb_sz);
    }
    return idx;
}
 
/* The same thing, but don't clear objects: */
/* returns the number of memory objects reclaimed/added to freelist */
STATIC signed_word MAK_reclaim_to_array_uninit(struct hblk *hbp, hdr *hhdr, size_t sz,
                               void** aflush_tb, word aflush_tb_sz,
                               void** list)
{

    word bit_no = 0;
    word *p, *plim;
    signed_word n_bytes_found = 0;
    signed_word idx = 0;


    MAK_ASSERT(sz == hhdr -> hb_sz);
    p = (word *)(hbp->hb_body);
    plim = (word *)((ptr_t)hbp + HBLKSIZE - sz);

    /* go through all words in block */
    while (p <= plim) {
       if( !mark_bit_from_hdr(hhdr, bit_no) ) {
           n_bytes_found += sz;
           /* object is available - put on list */
           list[idx]= p;
           idx++;
           set_mark_bit_from_hdr_unsafe(hhdr, bit_no);
       } 
       p = (word *)((ptr_t)p + sz);
       bit_no += MARK_BIT_OFFSET(sz);
    }
    hhdr -> hb_n_marks = HBLK_OBJS(sz);

    hhdr -> page_reclaim_state = IN_FLOATING;

    if (n_bytes_found != 0){
        MAK_NVM_ASYNC_RANGE_VIA(&(hhdr -> hb_n_marks), sizeof(hhdr -> hb_n_marks),
                     aflush_tb, aflush_tb_sz);
        MAK_NVM_ASYNC_RANGE_VIA(&(hhdr -> hb_marks), sizeof(hhdr -> hb_marks),
                     aflush_tb, aflush_tb_sz);
    }
    return idx;
}



MAK_INNER signed_word MAK_reclaim_to_array_generic(struct hblk * hbp,
                                  hdr *hhdr,
                                  size_t sz,
                                  MAK_bool init,
                                  hdr_cache_entry* hc,
                                  word hc_sz,
                                  void** aflush_tb,
                                  word aflush_tb_sz,
                                  void** list)
{
    signed_word result = 0;
    MAK_ASSERT(MAK_find_header((ptr_t)hbp) == hhdr);
    if (init) {
      result = MAK_reclaim_to_array_clear(hbp, hhdr, sz,
                            aflush_tb, aflush_tb_sz,
                            list);
    } else {
      MAK_ASSERT((hhdr)->hb_descr == 0 /* Pointer-free block */);
      result = MAK_reclaim_to_array_uninit(hbp, hhdr, sz,
                            aflush_tb, aflush_tb_sz,
                            list);
    }
    return result;
}



STATIC void MAK_reclaim_small_nonempty_block(struct hblk *hbp, hdr* hhdr)
{
    size_t sz = hhdr -> hb_sz;
    struct obj_kind * ok = &MAK_obj_kinds[hhdr -> hb_obj_kind];
    fl_hdr* flh = &(ok -> ok_freelist[BYTES_TO_GRANULES(sz)]);
    flh -> count = MAK_reclaim_to_array_generic(hbp,
                  hhdr,
                  sz,
                  ok -> ok_init,
                  &(MAK_hdr_cache[0]),
                  (word) HDR_CACHE_SIZE,
                  &(MAK_fl_aflush_table[0]),
                  (word) FL_AFLUSH_TABLE_SZ,
                  flh -> fl);
}

MAK_INNER void MAK_continue_reclaim(size_t gran /* granules */, int kind)
{
    hdr * hhdr;
    struct hblk * hbp;
    struct obj_kind * ok = &(MAK_obj_kinds[kind]);
    //first reclaim recent reclaim pages
    struct hblk ** rlh = MAK_reclaim_list[kind];
    if (rlh == 0) return;       /* No blocks of this kind.      */
    //TODO: should we check here that the fl_count is indeed zero.
    fl_hdr* flh = &(ok -> ok_freelist[gran]);
    rlh += gran;
    while ((hbp = *rlh) != 0) {

        hhdr = MAK_get_hdr_and_update_hc(hbp -> hb_body, MAK_hdr_cache, HDR_CACHE_SIZE);
        struct hblk* n_hb = hhdr -> hb_next;
        *rlh = n_hb;
        if (n_hb != NULL){
            hdr* n_hhdr = HDR(n_hb);
            n_hhdr -> hb_prev = NULL;
        }
        hhdr -> hb_next = NULL;
        MAK_reclaim_small_nonempty_block(hbp, hhdr);
        if (flh -> count > 0) return; 
    }
}
