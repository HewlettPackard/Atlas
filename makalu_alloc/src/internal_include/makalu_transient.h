#include "makalu_internal.h"

struct _MAK_transient_metadata {
    
    /*transient scratch memory */

    #define MAK_transient_scratch_free_ptr MAK_transient_md._transient_scratch_free_ptr
    ptr_t _transient_scratch_free_ptr;
    
    #define MAK_transient_scratch_end_ptr MAK_transient_md._transient_scratch_end_ptr
    ptr_t _transient_scratch_end_ptr;

    #define MAK_transient_scratch_last_end_ptr MAK_transient_md._scratch_last_end_ptr
    ptr_t _scratch_last_end_ptr;

};

MAK_EXTERN struct _MAK_transient_metadata MAK_transient_md;

MAK_INNER ptr_t MAK_transient_scratch_alloc(size_t bytes);

#define GET_MEM(bytes) MAK_get_transient_memory(bytes)
MAK_INNER ptr_t MAK_get_transient_memory(word bytes);





