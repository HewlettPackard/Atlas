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

