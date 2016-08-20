#ifndef _MAKALU_RECLAIM_H
#define _MAKALU_RECLAIM_H

MAK_INNER MAK_bool MAK_alloc_reclaim_list(unsigned k /* object kind */);
MAK_INNER void MAK_restart_block_freelists();
MAK_INNER signed_word MAK_build_array_fl(struct hblk *h, size_t sz_in_words, MAK_bool clear,
                           hdr_cache_entry* hc, word hc_sz,
                           void** aflush_tb, word aflush_tb_sz,
                           void** list);
MAK_INNER void MAK_continue_reclaim(size_t gran /* granules */, int kind);
MAK_INNER signed_word MAK_reclaim_to_array_generic(struct hblk * hbp,
                                  hdr *hhdr,
                                  size_t sz,
                                  MAK_bool init,
                                  hdr_cache_entry* hc,
                                  word hc_sz,
                                  void** aflush_tb,
                                  word aflush_tb_sz,
                                  void** list);

MAK_INNER void MAK_truncate_freelist(fl_hdr* flh, word ngranules, int k,
                         word keep, word max,
                         hdr_cache_entry* hc, word hc_sz,
                         void** aflush_tb, word aflush_tb_sz);

MAK_INNER void MAK_truncate_fast_fl(fl_hdr* flh, word ngranules, int k,
                         word keep, word max,
                         hdr_cache_entry* hc, word hc_sz,
                         void** aflush_tb, word aflush_tb_sz);

MAK_INNER signed_word MAK_build_fl_array_from_mark_bits_clear(struct hblk* hbp,
       word* mark_bits,
       size_t sz,
       void** list);
MAK_INNER signed_word MAK_build_fl_array_from_mark_bits_uninit(struct hblk* hbp,
       word* mark_bits,
       size_t sz,
       void** list);


#endif
