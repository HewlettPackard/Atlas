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

#ifndef _MAKALU_BLOCK_H
#define _MAKALU_BLOCK_H

#define FL_UNKNOWN -1

# define INCR_FREE_BYTES(n, b) MAK_free_bytes[n] = MAK_free_bytes[n] + (b);

MAK_INNER MAK_bool MAK_expand_hp_inner(word n);
MAK_INNER MAK_bool MAK_finish_expand_hp_inner();
MAK_INNER void MAK_add_to_heap(struct hblk *space, size_t bytes, word expansion_slop);
MAK_INNER void MAK_newfreehblk(struct hblk *hbp, word size);
MAK_INNER struct hblk *
MAK_allochblk(size_t sz, int kind, unsigned flags/* IGNORE_OFF_PAGE or 0 */);

MAK_INNER void MAK_new_hblk(size_t gran, int kind);
MAK_INNER MAK_bool MAK_try_expand_hp(word needed_blocks,
       MAK_bool ignore_off_page, MAK_bool retry);
MAK_INNER void MAK_freehblk(struct hblk *hbp);

#endif
