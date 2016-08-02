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

    #define MAK_fl_max_count MAK_transient_md._fl_max_count
    word _fl_max_count[MAXOBJGRANULES+1];

    #define MAK_fl_optimal_count MAK_transient_md._fl_optimal_count
    word _fl_optimal_count[MAXOBJGRANULES+1];

    #define MAK_hblkfreelist MAK_transient_md._hblkfreelist
    struct hblk* _hblkfreelist[N_HBLK_FLS+1];

    #define MAK_free_bytes MAK_transient_md._free_bytes
    word _free_bytes[N_HBLK_FLS+1];

    #define MAK_n_heap_sects MAK_transient_md._n_heap_sects
    word _n_heap_sects;

    # define MAK_heap_sects MAK_transient_md._heap_sects
    HeapSect _heap_sects[MAX_HEAP_SECTS];        /* Heap segments potentially  */ 

};


MAK_INNER MAK_bool MAK_alloc_reclaim_list(unsigned k);


MAK_EXTERN struct _MAK_transient_metadata MAK_transient_md;

MAK_INNER ptr_t MAK_transient_scratch_alloc(size_t bytes);

#define GET_MEM(bytes) MAK_get_transient_memory(bytes)
MAK_INNER ptr_t MAK_get_transient_memory(word bytes);





