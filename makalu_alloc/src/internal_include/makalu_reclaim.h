#ifndef _MAKALU_RECLAIM_H
#define _MAKALU_RECLAIM_H

MAK_INNER MAK_bool MAK_alloc_reclaim_list(unsigned k /* object kind */);
MAK_INNER void MAK_restart_block_freelists();

#endif
