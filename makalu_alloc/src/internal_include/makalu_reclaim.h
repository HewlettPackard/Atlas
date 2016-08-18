#ifndef _MAKALU_RECLAIM_H
#define _MAKALU_RECLAIM_H

MAK_INNER MAK_bool MAK_alloc_reclaim_list(unsigned k /* object kind */);
MAK_INNER void MAK_restart_block_freelists();
MAK_INNER signed_word MAK_build_array_fl(struct hblk *h, size_t sz_in_words, MAK_bool clear,
                           hdr_cache_entry* hc, word hc_sz,
                           void** aflush_tb, word aflush_tb_sz,
                           void** list);
#endif
