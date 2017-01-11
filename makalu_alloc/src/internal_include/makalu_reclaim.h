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
 */

#ifndef _MAKALU_RECLAIM_H
#define _MAKALU_RECLAIM_H

MAK_INNER MAK_bool MAK_alloc_reclaim_list(unsigned k /* object kind */);
MAK_INNER void MAK_restart_block_freelists();
MAK_INNER signed_word MAK_build_array_fl(struct hblk *h, size_t sz_in_words, MAK_bool clear,
                           hdr_cache_entry* hc, word hc_sz,
                           void** aflush_tb, word aflush_tb_sz,
                           void** list);
MAK_INNER void MAK_continue_reclaim(size_t gran /* granules */, int kind);
MAK_INNER void MAK_defer_reclaim_block(struct hblk *hbp, word flag);

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
MAK_INNER MAK_bool MAK_block_empty(hdr *hhdr);

#endif
