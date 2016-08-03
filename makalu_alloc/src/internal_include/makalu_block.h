#ifndef _MAKALU_BLOCK_H
#define _MAKALU_BLOCK_H

#define FL_UNKNOWN -1

# define INCR_FREE_BYTES(n, b) MAK_free_bytes[n] = MAK_free_bytes[n] + (b);

MAK_INNER MAK_bool MAK_expand_hp_inner(word n);
MAK_INNER MAK_bool MAK_finish_expand_hp_inner();
MAK_INNER void MAK_add_to_heap(struct hblk *space, size_t bytes, word expansion_slop);
MAK_INNER void MAK_newfreehblk(struct hblk *hbp, word size);


#endif
