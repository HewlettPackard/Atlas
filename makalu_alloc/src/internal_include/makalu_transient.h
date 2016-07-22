#include "makalu_internal.h"

typedef struct MAK_fl_hdr {
     void** fl;
     word start_idx;
     signed_word count;
} fl_hdr;


struct _MAK_transient_metadata {
    
    /*transient scratch memory */

    #define MAK_transient_scratch_free_ptr MAK_transient_md._transient_scratch_free_ptr
    ptr_t _transient_scratch_free_ptr;
    
    #define MAK_transient_scratch_end_ptr MAK_transient_md._transient_scratch_end_ptr
    ptr_t _transient_scratch_end_ptr;

    #define MAK_transient_scratch_last_end_ptr MAK_transient_md._scratch_last_end_ptr
    ptr_t _scratch_last_end_ptr;

    #define MAK_objfreelist MAK_transient_md._objfreelist
    fl_hdr _objfreelist[MAXOBJGRANULES+1];

    #define MAK_aobjfreelist MAK_transient_md._aobjfreelist
    fl_hdr _aobjfreelist[MAXOBJGRANULES+1];

    #define MAK_uobjfreelist MAK_transient_md._uobjfreelist
                          /* Uncollectible but traced objs      */
                          /* objects on this and auobjfreelist  */
                          /* are always marked, except during   */
                          /* garbage collections.  */
    fl_hdr _uobjfreelist[MAXOBJGRANULES+1];

    #define MAK_reclaim_list MAK_transient_md._reclaim_list
    struct hblk** _reclaim_list[MAXOBJKINDS];
};


MAK_INNER MAK_bool MAK_alloc_reclaim_list(unsigned k);


MAK_EXTERN struct _MAK_transient_metadata MAK_transient_md;

MAK_INNER ptr_t MAK_transient_scratch_alloc(size_t bytes);

#define GET_MEM(bytes) MAK_get_transient_memory(bytes)
MAK_INNER ptr_t MAK_get_transient_memory(word bytes);





