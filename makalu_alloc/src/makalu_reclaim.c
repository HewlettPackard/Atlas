#include "makalu_internal.h"

MAK_INNER MAK_bool MAK_alloc_reclaim_list(unsigned k /*kind */)
{
    struct hblk** result;
    //then reclaim list can be transient
    result = (struct hblk **) MAK_transient_scratch_alloc(
              (MAXOBJGRANULES+1) * sizeof(struct hblk *));
    if (result == 0) return(FALSE);
    BZERO(result, (MAXOBJGRANULES+1)*sizeof(struct hblk *));
    MAK_reclaim_list[k] = result;
   
    return (TRUE);
}
