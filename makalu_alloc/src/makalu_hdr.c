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
 *   headers.c
 *
 *   Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 *   Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
 *   Copyright (c) 1996 by Silicon Graphics.  All rights reserved.
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

#include "makalu_internal.h"
#include "stddef.h"

/*
 * This implements:
 * 1. allocation of heap block headers
 * 2. A map from addresses to heap block addresses to heap block headers
 *
 * Access speed is crucial.  We implement an index structure based on a 2
 * level tree.
 */

MAK_INNER bottom_index* MAK_all_nils = NULL;
MAK_INNER bottom_index** MAK_top_index;





MAK_INNER hdr * MAK_find_header(ptr_t h)
{
        hdr * result;
        GET_HDR(h, result);
        return(result);
}

/* Handle a header cache miss.  Returns a pointer to the        */
/* header corresponding to p, if p can possibly be a valid      */
/* object pointer, and 0 otherwise.                             */
/* GUARANTEED to return 0 for a pointer past the first page     */
/* of an object unless both MAK_all_interior_pointers is set     */
/* and p is in fact a valid object pointer.                     */
/* Never returns a pointer to a free hblk.                      */

hdr_cache_entry MAK_hdr_cache[HDR_CACHE_SIZE];

MAK_INNER hdr * MAK_header_cache_miss(ptr_t p, hdr_cache_entry *hce)
{
  hdr *hhdr;
  GET_HDR(p, hhdr);
  if (IS_FORWARDING_ADDR_OR_NIL(hhdr)) {
    if (MAK_all_interior_pointers) {
      if (hhdr != 0) {
        ptr_t current = p;

        current = (ptr_t)HBLKPTR(current);
        do {
            current = current - HBLKSIZE*(word)hhdr;
            hhdr = HDR(current);
        } while(IS_FORWARDING_ADDR_OR_NIL(hhdr));
        /* current points to near the start of the large object */
        if (hhdr -> hb_flags & IGNORE_OFF_PAGE)
            return 0;
        if (HBLK_IS_FREE(hhdr)
            || p - current >= (ptrdiff_t)(hhdr->hb_sz)) {
            /* Pointer past the end of the block */
            return 0;
        }
      } 
      MAK_ASSERT(hhdr == 0 || !HBLK_IS_FREE(hhdr));
      return hhdr;
      /* Pointers past the first page are probably too rare     */
      /* to add them to the cache.  We don't.                   */
      /* And correctness relies on the fact that we don't.      */
    } else {
      return 0;
    }
  } else {
    if (HBLK_IS_FREE(hhdr)) {
      return 0;
    } else {
      hce -> block_addr = (word)(p) >> LOG_HBLKSIZE;
      hce -> hce_hdr = hhdr;
      return hhdr;
    }
  }
}

MAK_INNER void MAK_update_hc(ptr_t p, hdr* hhdr, hdr_cache_entry* hc, word hc_sz){
    hdr_cache_entry* entry = HCE(p, hc, hc_sz);
    entry -> block_addr = ((word) (p)) >> LOG_HBLKSIZE;
    //if (HBLKPTR(p) != hhdr -> hb_block){
    //    MAK_printf("hhdr block: %p, mismatched HBLKPTR(p): %p\n", hhdr -> hb_block, p);
    //}
    entry -> hce_hdr = hhdr;
}

MAK_INNER hdr* MAK_get_hdr_no_update(ptr_t p, hdr_cache_entry* hc, word hc_sz){
     hdr_cache_entry * hce = HCE(p, hc, hc_sz);
     hdr* hhdr;
     if (EXPECT(HCE_VALID_FOR(hce, p), TRUE)) {
         hhdr = hce -> hce_hdr;
         if (EXPECT(hhdr -> hb_block == HBLKPTR(p), TRUE)){
             //MAK_printf("Header found in cache\n");
             return hhdr;
         }
     }
     return HDR(p);
}

MAK_INNER hdr* MAK_get_hdr_and_update_hc(ptr_t p, hdr_cache_entry* hc, word hc_sz){
     hdr_cache_entry * hce = HCE(p, hc, hc_sz);
     hdr* hhdr;
     if (EXPECT(HCE_VALID_FOR(hce, p), TRUE)) {
         hhdr = hce -> hce_hdr;
         if (EXPECT(hhdr -> hb_block == HBLKPTR(p), TRUE)){
             //MAK_printf("Header found in cache\n");
             return hhdr;
         }
     }
     hhdr = HDR(p);
     //if (HBLKPTR(p) != hhdr -> hb_block){
     // MAK_printf("hhdr block: %p, mismatched HBLKPTR(p): %p\n", hhdr -> hb_block, p);
     //}

     hce -> block_addr = ((word)(hhdr -> hb_block)) >> LOG_HBLKSIZE;
     hce -> hce_hdr = hhdr;
     return hhdr;
}

static ptr_t MAK_hdr_end_ptr = NULL;
static word MAK_curr_hdr_space = 0;
static ptr_t MAK_hdr_idx_end_ptr = NULL;
static word MAK_curr_hdr_idx_space = 0;

static inline MAK_bool scratch_alloc_hdr_space(word bytes_to_get)
{
    word i;

    //we have some memory to use;
    if (MAK_curr_hdr_space < MAK_n_hdr_spaces){
         i = MAK_curr_hdr_space;
         goto out;
    }

    if (MAK_n_hdr_spaces >= MAX_HEAP_SECTS)
        ABORT("Max heap sectors reached! Cannot allocate any more header for the heap pages!");
    i = MAK_n_hdr_spaces;
    MAK_STORE_NVM_SYNC(MAK_n_hdr_spaces, MAK_n_hdr_spaces + 1);
    MAK_STORE_NVM_SYNC(MAK_hdr_spaces[i].hs_bytes, bytes_to_get);
    int res = GET_MEM_PERSISTENT(&(MAK_hdr_spaces[i].hs_start), bytes_to_get);
    if (res != 0){
        MAK_STORE_NVM_SYNC(MAK_n_hdr_spaces, i);
        WARN("Could not acquire space for headers!\n", 0);
        return FALSE;
    }
out:
    //we initialize the free pointer to point to the 
    //proper sector on restart, when the first header is allocated
    //in that sector, we guarantee the free pointer to be visible in nvram at the end of the correspoding transaction
    MAK_hdr_free_ptr = MAK_hdr_spaces[i].hs_start;
    MAK_hdr_end_ptr = MAK_hdr_free_ptr + bytes_to_get;
    return TRUE;
}

static hdr* alloc_hdr(void)
{
    register hdr* result;
    if (MAK_hdr_free_list != 0)
    {
        result = MAK_hdr_free_list;
        //incase of a crash, we rebuild the freelist, 
        //so it is ok to be out of sync with the transaction
        MAK_hdr_free_list = (hdr *) (result -> hb_next);
        result->hb_prev = result->hb_next = 0;
        //MAK_printf("Hdr being allocated: %p\n", result);
        //headers in the header freelist have all size set to zero, 
        //but we need to log it so that
        //undoing of a transaction can leave it size = 0, 
        //and we can add to freelist when scanning
        MAK_LOG_NVM_WORD(&(result->hb_sz), 0);
        return result;
    }

    result = (hdr*) MAK_hdr_free_ptr;
    ptr_t new_free_ptr = MAK_hdr_free_ptr + sizeof (hdr);
    if (new_free_ptr <= MAK_hdr_end_ptr){
        //it is important to be in sync with the transaction, 
        //because that upto where we scan to for headers
        MAK_NO_LOG_STORE_NVM(MAK_hdr_free_ptr, new_free_ptr);
        //headers size does not need to be set to zero because the 
        //free_ptr would be rewinded in case of a crash
        //MAK_printf("%dth header allocated\n", hdr_alloc_count);
        return (result);
    }
    //we expand the hdr scratch space based on the 
    //size of previous heap expansion size
    word bytes_to_get = MINHINCR * HBLKSIZE;
    if (MAK_last_heap_size > 0){
        word h_blocks = divHBLKSZ(MAK_last_heap_size);
        word btg = (OBJ_SZ_TO_BLOCKS(h_blocks * sizeof(hdr))) * HBLKSIZE;
        if (btg > bytes_to_get) bytes_to_get = btg;
    }
    MAK_curr_hdr_space++;
    MAK_bool res = scratch_alloc_hdr_space(bytes_to_get);
    if (!res && bytes_to_get > MINHINCR * HBLKSIZE)
        res =  scratch_alloc_hdr_space(MINHINCR * HBLKSIZE);
    if (!res)
        res =  scratch_alloc_hdr_space(HBLKSIZE);
    if (!res)
        ABORT("Could not acquire even a page for header space!\n");
    return alloc_hdr();
}

static inline MAK_bool scratch_alloc_hdr_idx_space(word bytes_to_get)
{
    word i;

    if (MAK_curr_hdr_idx_space < MAK_n_hdr_idx_spaces) {
        i = MAK_curr_hdr_idx_space;
        goto out;
    }

    if (MAK_n_hdr_idx_spaces >= MAX_HEAP_SECTS)
        ABORT("Max heap sectors reached! Cannot create header index for the heap pages!");
    i = MAK_n_hdr_idx_spaces;
    MAK_STORE_NVM_SYNC(MAK_n_hdr_idx_spaces, MAK_n_hdr_idx_spaces + 1);
    MAK_STORE_NVM_SYNC(MAK_hdr_idx_spaces[i].hs_bytes, bytes_to_get);
    int res = GET_MEM_PERSISTENT(&(MAK_hdr_idx_spaces[i].hs_start), bytes_to_get);
    if (res != 0){
        MAK_STORE_NVM_SYNC(MAK_n_hdr_idx_spaces, i);
        WARN("Could not acquire space for header indices!\n", 0);
        return FALSE;
    }
out:
    //no need to be synchronous to the computation here, 
    //we rebuild it from scratch in the case of a crash
    MAK_hdr_idx_free_ptr = MAK_hdr_idx_spaces[i].hs_start;
    MAK_hdr_idx_end_ptr = MAK_hdr_idx_free_ptr + bytes_to_get;
    return TRUE;
}

static bottom_index* alloc_bi(void)
{
    register bottom_index* result;

    result = (bottom_index*) MAK_hdr_idx_free_ptr;
    ptr_t new_free_ptr = MAK_hdr_idx_free_ptr + sizeof (bottom_index);
    if (new_free_ptr <= MAK_hdr_idx_end_ptr){
        //incase of a crash we reset the free_ptr
        MAK_hdr_idx_free_ptr = new_free_ptr;
        //MAK_printf("%dth bottom index allocated\n", alloc_bi_count);
        return (result);
    }
    word bytes_to_get = MINHINCR * HBLKSIZE;
    MAK_curr_hdr_idx_space++;
    MAK_bool res = scratch_alloc_hdr_idx_space(bytes_to_get);
    if (!res)
        res =  scratch_alloc_hdr_idx_space(HBLKSIZE);
    if (!res)
        ABORT("Could not even acquire a page for header indices!\n");
    return alloc_bi();
}

MAK_INNER void MAK_init_headers(void)
{
    //all visibility concerns here are addressed
    //allocate initial header space
    word bytes_to_get = MINHINCR * HBLKSIZE;
    MAK_curr_hdr_space = 0;
    MAK_n_hdr_spaces = 0;
    scratch_alloc_hdr_space(bytes_to_get);

    //allocate initial header index space
    bytes_to_get = MINHINCR * HBLKSIZE;
    MAK_curr_hdr_idx_space = 0;
    MAK_n_hdr_idx_spaces = 0;
    scratch_alloc_hdr_idx_space(bytes_to_get);

    register unsigned i;
    for (i = 0; i < TOP_SZ; i++) {
        MAK_top_index[i] = MAK_all_nils;
    }
}

MAK_INNER void MAK_remove_header(struct hblk *h)
{
    hdr **ha;
    GET_HDR_ADDR(h, ha);
    //free_hdr(*ha);
    hdr* hhdr = *ha;
    //we don't log because whoever called this to be removed has already logged the appropriate fields in the header
    //e.g. MAK_newfreehblk calls MAK_remove_from_fl before it calls this function which makes sure that hb_prev, hb_next, and hb_sz is logged
    MAK_NO_LOG_STORE_NVM(hhdr->hb_sz, 0);
    //The below is necessary for header cache to work properly
    MAK_STORE_NVM_ADDR(&(hhdr -> hb_block), 0);
    MAK_STORE_NVM_ASYNC(hhdr -> hb_next, (struct hblk*) MAK_hdr_free_list);
    //will be made visible before exit
    MAK_hdr_free_list = hhdr;
    MAK_STORE_NVM_PTR_ASYNC(ha, 0);
}

/* Make sure that there is a bottom level index block for address addr  */
/* Return FALSE on failure.                                             */
static MAK_bool get_index(word addr)
{
    word hi = (word)(addr) >> (LOG_BOTTOM_SZ + LOG_HBLKSIZE);
    bottom_index * r;
    bottom_index * p;
    bottom_index ** prev;
    bottom_index *pi;

    word i = TL_HASH(hi);
    bottom_index * old;

    old = p = MAK_top_index[i];
    while(p != MAK_all_nils) {
        if (p -> key == hi) return(TRUE);
        p = p -> hash_link;
    }
    r = alloc_bi();
    if (r == 0) return(FALSE);
    BZERO(r, sizeof (bottom_index));
    r -> hash_link = old;
    //no need to log changes to r because in case of failure r. 
    //Relies on the original value of MAK_scratch_free_ptr being logged before the
    //start of any allocation
    //during MAK_free, the MAK_top_index is read without a lock held. So, we have to
    //ensure that the write is a 64-bit write instead of 2 32 bytes write which compiler
    //can generate
    ENSURE_64_BIT_COPY(MAK_top_index[i], r);
    MAK_NVM_ASYNC_RANGE(&(MAK_top_index[i]), sizeof(bottom_index*));
    r -> key = hi;
    /* Add it to the list of bottom indices */
    prev = &MAK_all_bottom_indices;    /* pointer to p */
    pi = 0;                           /* bottom_index preceding p */
    while ((p = *prev) != 0 && p -> key < hi) {
      pi = p;
      prev = &(p -> asc_link);
    }
    r -> desc_link = pi;
    if (0 == p) {
      MAK_STORE_NVM_ASYNC(MAK_all_bottom_indices_end, r);
    } else {
      MAK_STORE_NVM_ASYNC(p -> desc_link, r);
    }
    r -> asc_link = p;
    //*prev = r;
    MAK_STORE_NVM_PTR_ASYNC(prev, r);
    MAK_NVM_ASYNC_RANGE(r, sizeof(bottom_index));
    return(TRUE);
}

MAK_INNER void MAK_restart_persistent_scratch_alloc(){
    int  i;
    ptr_t end;
    ptr_t start;
    ptr_t free_ptr = MAK_hdr_free_ptr;
    i = (int) MAK_n_hdr_spaces - 1;
    //last memory acquisition was incomplete
    if (MAK_hdr_spaces[i].hs_start == NULL){
        //don't really need to flush it. We will get to it next time
        //we do need to fix it.
        MAK_STORE_NVM_ASYNC(MAK_n_hdr_spaces, MAK_n_hdr_spaces - 1);
        i--;
    }

    //if we allocated some memory for header but died before doing any allocation from it
    if (free_ptr == 0)
    {
        i = 0;
        MAK_curr_hdr_space = 0;
        MAK_hdr_free_ptr = MAK_hdr_spaces[i].hs_start;
        MAK_hdr_end_ptr = MAK_hdr_free_ptr + MAK_hdr_spaces[i].hs_bytes;
    }
    else { //if not we have a valid free_ptr
        MAK_curr_hdr_space = MAK_n_hdr_spaces;
        MAK_hdr_end_ptr = 0;
        for ( ; i >= 0; i--){
            start = MAK_hdr_spaces[i].hs_start;
            end = start + MAK_hdr_spaces[i].hs_bytes;
            if (free_ptr >= start && free_ptr < end){
                MAK_curr_hdr_space = (word) i;
                MAK_hdr_end_ptr = end;
                break;
            }
       }
    }

    //no need to restart the idx allocation since it has to be rebuild anyway
    if (MAK_persistent_state != PERSISTENT_STATE_NONE)
        return;

    free_ptr = MAK_hdr_idx_free_ptr;
    i = (int) MAK_n_hdr_idx_spaces - 1;
    //last memory acquisition was incomplete
    if (MAK_hdr_idx_spaces[i].hs_start == NULL){
       MAK_STORE_NVM_ASYNC(MAK_n_hdr_idx_spaces, MAK_n_hdr_idx_spaces - 1);
       i--;
    }


    //see the comment for headers
    if (free_ptr == 0)
    {
        i = 0;
        MAK_curr_hdr_idx_space = 0;
        MAK_hdr_idx_free_ptr = MAK_hdr_idx_spaces[i].hs_start;
        MAK_hdr_idx_end_ptr = MAK_hdr_idx_free_ptr + MAK_hdr_idx_spaces[i].hs_bytes;
    }
    else
    {
        MAK_curr_hdr_idx_space = MAK_n_hdr_idx_spaces;
        MAK_hdr_idx_end_ptr = 0;
        for ( ; i >=0; i--){
            start = MAK_hdr_idx_spaces[i].hs_start;
            end = start + MAK_hdr_idx_spaces[i].hs_bytes;
            if (free_ptr >= start && free_ptr < end){
                MAK_curr_hdr_idx_space = (word) i;
                MAK_hdr_idx_end_ptr = end;
                break;
           }
        }
    }
}

MAK_INNER void MAK_rebuild_metadata_from_headers()
{
    //rewind the header_idx space
    MAK_hdr_idx_free_ptr = NULL;
    MAK_curr_hdr_idx_space = 0;
    MAK_hdr_idx_end_ptr = NULL;
    MAK_all_bottom_indices_end = 0;
    MAK_all_bottom_indices = 0;
    if (!scratch_alloc_hdr_idx_space(MINHINCR * HBLKSIZE))
        ABORT("Metadata rebuild unsuccessful: Could not allocate header idx space!\n");

    register unsigned i;
    for (i = 0; i < TOP_SZ; i++) {
        MAK_STORE_NVM_ASYNC(MAK_top_index[i], MAK_all_nils);
    }

    word space = MAK_n_hdr_spaces - 1;
    //fix the last unsuccessful acquisition
    if (MAK_hdr_spaces[space].hs_start == NULL){
        //don't really need to flush it. We will get to it next time
        //we do need to fix it.
        MAK_STORE_NVM_ASYNC(MAK_n_hdr_spaces, MAK_n_hdr_spaces - 1);
    }

    space = MAK_n_hdr_idx_spaces - 1;

    //last memory acquisition was incomplete
    if (MAK_hdr_idx_spaces[space].hs_start == NULL){
       MAK_STORE_NVM_ASYNC(MAK_n_hdr_idx_spaces, MAK_n_hdr_idx_spaces - 1);
    }

    hdr* hdr_fl = NULL;

    //word free_bytes[N_HBLK_FLS+1];
    //BZERO(free_bytes, sizeof(word) * (N_HBLK_FLS + 1));

    register hdr* curr;
    ptr_t end;
    struct hblk* h;
    struct hblk* second;
    //hdr* second_hdr;
    word sz;
    word n_blocks;
    //int fl_index;
    //int count = 0; 
    for(space = 0; space < MAK_n_hdr_spaces; space++)
    {
        curr = (hdr*) MAK_hdr_spaces[space].hs_start;
        end  = ( (ptr_t) curr ) + MAK_hdr_spaces[space].hs_bytes;
        if (MAK_hdr_free_ptr >= (ptr_t) curr && MAK_hdr_free_ptr < end)
            end = MAK_hdr_free_ptr;
        for (; (((ptr_t) curr) + sizeof(hdr)) <= end; curr++){
            sz = curr -> hb_sz;

            //MAK_printf("Scanning header %d\n", count);
            //count++;
            //if (count == 1172)
            //{
            //   count = count + 1 - 1;
            //   MAK_printf("Happy to die!\n");
            //}

            //if its an unallocated header 
            if (sz == 0){
               MAK_STORE_NVM_ASYNC(curr -> hb_next, (struct hblk*) hdr_fl);
               //curr->hb_next = (struct hblk*) hdr_fl;
               hdr_fl = curr;
               continue;
            }

            h = curr -> hb_block;
            //if (h == 0)
            //   i("FOUND an allocated header with no block addr\n");

            if (!get_index((word)h)){
                goto out;
            }
            SET_HDR(h, curr);
            //if its a free block
            if (HBLK_IS_FREE(curr)){
                continue;
            }
            //its an allocated block
            //install counts
            sz = HBLKSIZE * OBJ_SZ_TO_BLOCKS(sz);

            for (second = h + BOTTOM_SZ; 
             (char*) second < (char*)h + sz; 
             second += BOTTOM_SZ) {
                if (!get_index((word)second)) goto out;
            }
            if (!get_index((word)h + sz - 1))
                goto out;
            for (second = h + 1; (char*) second < (char*) h + sz; second += 1) {
                n_blocks = HBLK_PTR_DIFF(second, h);
                SET_HDR(second, (hdr *)(n_blocks > MAX_JUMP? MAX_JUMP : n_blocks));
            }
        }
    }
    //No need to flush here. Will be flushed by MAK_sync_alloc_metadata
    MAK_hdr_free_list = hdr_fl;

    return;

out:
   ABORT("Could not install the index for a header!\n");
}

/* Install a header for block h.        */
/* The header is uninitialized.         */
/* Returns the header or 0 on failure.  */
MAK_INNER struct hblkhdr * MAK_install_header(struct hblk *h)
{
    hdr * result;

    if (!get_index((word) h)) return(0);
    result = alloc_hdr();
    //we need this information in every header to 
    //process the headers in the case of a crash
    MAK_NO_LOG_STORE_NVM(result->hb_block, h);

    if (result) {
      SET_HDR(h, result);
    }
    return(result);
}

/* Set up forwarding counts for block h of size sz */
MAK_INNER MAK_bool MAK_install_counts(struct hblk *h, size_t sz/* bytes */)
{
    struct hblk * hbp;
    word i;

    for (hbp = h; (char *)hbp < (char *)h + sz; hbp += BOTTOM_SZ) {
        if (!get_index((word) hbp)) return(FALSE);
    }
    if (!get_index((word)h + sz - 1)) return(FALSE);
    for (hbp = h + 1; (char *)hbp < (char *)h + sz; hbp += 1) {
        i = HBLK_PTR_DIFF(hbp, h);
        SET_HDR(hbp, (hdr *)(i > MAX_JUMP? MAX_JUMP : i));
    }
    return(TRUE);
}

/* Remove forwarding counts for h */
MAK_INNER void MAK_remove_counts(struct hblk *h, size_t sz/* bytes */)
{
    register struct hblk * hbp;
    for (hbp = h+1; (char *)hbp < (char *)h + sz; hbp += 1) {
        SET_HDR(hbp, 0);
    }
}

/* Apply fn to all allocated blocks */
/*VARARGS1*/
MAK_INNER void MAK_apply_to_all_blocks(void (*fn)(struct hblk *h, word client_data),
                            word client_data)
{
    signed_word j;
    bottom_index * index_p;

    for (index_p = MAK_all_bottom_indices; index_p != 0;
         index_p = index_p -> asc_link) {
        for (j = BOTTOM_SZ-1; j >= 0;) {
            if (!IS_FORWARDING_ADDR_OR_NIL(index_p->index[j])) {
                if (!HBLK_IS_FREE(index_p->index[j])) {
                    (*fn)(((struct hblk *)
                              (((index_p->key << LOG_BOTTOM_SZ) + (word)j)
                               << LOG_HBLKSIZE)),
                          client_data);
                }
                j--;
             } else if (index_p->index[j] == 0) {
                j--;
             } else {
                j -= (signed_word)(index_p->index[j]);
             }
         }
     }
}

/* Get the next valid block whose address is at least h */
/* Return 0 if there is none.                           */
MAK_INNER struct hblk * MAK_next_used_block(struct hblk *h)
{
    register bottom_index * bi;
    register word j = ((word)h >> LOG_HBLKSIZE) & (BOTTOM_SZ-1);

    GET_BI(h, bi);
    if (bi == MAK_all_nils) {
        register word hi = (word)h >> (LOG_BOTTOM_SZ + LOG_HBLKSIZE);
        bi = MAK_all_bottom_indices;
        while (bi != 0 && bi -> key < hi) bi = bi -> asc_link;
        j = 0;
    }
    while(bi != 0) {
        while (j < BOTTOM_SZ) {
            hdr * hhdr = bi -> index[j];
            if (IS_FORWARDING_ADDR_OR_NIL(hhdr)) {
                j++;
            } else {
                if (!HBLK_IS_FREE(hhdr)) {
                    return((struct hblk *)
                              (((bi -> key << LOG_BOTTOM_SZ) + j)
                               << LOG_HBLKSIZE));
                } else {
                    j += divHBLKSZ(hhdr -> hb_sz);
                }
            }
        }
        j = 0;
        bi = bi -> asc_link;
    }
    return(0);
}

/* Get the last (highest address) block whose address is        */
/* at most h.  Return 0 if there is none.                       */
/* Unlike the above, this may return a free block.              */
MAK_INNER struct hblk * MAK_prev_block(struct hblk *h)
{
    register bottom_index * bi;
    register signed_word j = ((word)h >> LOG_HBLKSIZE) & (BOTTOM_SZ-1);

    GET_BI(h, bi);
    if (bi == MAK_all_nils) {
        register word hi = (word)h >> (LOG_BOTTOM_SZ + LOG_HBLKSIZE);
        bi = MAK_all_bottom_indices_end;
        while (bi != 0 && bi -> key > hi) bi = bi -> desc_link;
        j = BOTTOM_SZ - 1;
    }
    while(bi != 0) {
        while (j >= 0) {
            hdr * hhdr = bi -> index[j];
            if (0 == hhdr) {
                --j;
            } else if (IS_FORWARDING_ADDR_OR_NIL(hhdr)) {
                j -= (signed_word)hhdr;
            } else {
                return((struct hblk *)
                          (((bi -> key << LOG_BOTTOM_SZ) + j)
                               << LOG_HBLKSIZE));
            }
        }
        j = BOTTOM_SZ - 1;
        bi = bi -> desc_link;
    }
    return(0);
}

/* Given a pointer, this method returns the pointer */
/* to the base of the memory object given a valid pointer */
/* within the object. Else, it returns zero. */
/* For a valid pointer it also returns */
/* the corresponding header hct_size is expected */
/* to be multiples of 2. Called without */
/* the allocation lock held */

MAK_INNER void* MAK_hc_base_with_hdr(void *p, 
  hdr_cache_entry *hct, unsigned int hct_sz, hdr** hdr_ret){
    ptr_t r;
    struct hblk *h;
    bottom_index *bi;
    hdr *candidate_hdr;
    hdr_cache_entry* hce;
    ptr_t limit;
    *hdr_ret = NULL;
    int hce_valid = 0;
    if (!MAK_is_initialized) return 0;
    r = p;
    h = HBLKPTR(r);
    hce = hct + (((word)(r) >> LOG_HBLKSIZE) & (hct_sz - 1));
    if (EXPECT(HCE_VALID_FOR(hce, r), 1)) {
        candidate_hdr = (hce -> hce_hdr);
        if (EXPECT(candidate_hdr -> hb_block == h, TRUE)){
            hce_valid = 1;
            //MAK_printf("[MAK_hc_base_with_hdr] Header found in cache\n");
        }
    }
    if (hce_valid == 0) {
        GET_BI(r, bi);
        candidate_hdr = HDR_FROM_BI(bi, r);
        if (candidate_hdr == 0) return(0);
        while (IS_FORWARDING_ADDR_OR_NIL(candidate_hdr)) {
           h = FORWARDED_ADDR(h,candidate_hdr);
           r = (ptr_t)h;
           candidate_hdr = HDR(h);
        }
        if (HBLK_IS_FREE(candidate_hdr)) return(0);
    }
    r = (ptr_t)((word)r & ~(WORDS_TO_BYTES(1) - 1));
    {
        size_t offset = HBLKDISPL(r);
        word sz = candidate_hdr -> hb_sz;
        size_t obj_displ = offset % sz;

        r -= obj_displ;
        limit = r + sz;
        if (limit > (ptr_t)(h + 1) && sz <= HBLKSIZE) {
            return(0);
        }
        if ((ptr_t)p >= limit) {
            return(0);
        }
    }

    if (hce_valid == 0)
    {
        MAK_update_hc(r, candidate_hdr, hct, hct_sz);
    }

    *hdr_ret = candidate_hdr;
    return ((void*)r);
}



