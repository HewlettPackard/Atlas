#ifndef _MAKALU_BASE_H
#define _MAKALU_BASE_H


typedef struct {
    ptr_t hs_start;
    size_t hs_bytes;
} HeapSect;


#define MAK_base_md (*(MAK_base_md_ptr))
MAK_EXTERN struct MAK_base_md* MAK_base_md_ptr;

struct MAK_base_md {

    /* header metadata*/
    #define MAK_hdr_spaces MAK_base_md._hdr_spaces
    HeapSect _hdr_spaces[MAX_HEAP_SECTS];
 
    #define MAK_n_hdr_spaces MAK_base_md._n_hdr_spaces
    word _n_hdr_spaces;
  
    #define MAK_hdr_free_ptr MAK_base_md._hdr_free_ptr
    ptr_t _hdr_free_ptr;

    #define MAK_hdr_idx_spaces MAK_base_md._hdr_idx_spaces
    HeapSect _hdr_idx_spaces[MAX_HEAP_SECTS];

    #define MAK_n_hdr_idx_spaces MAK_base_md._n_hdr_idx_spaces
    word _n_hdr_idx_spaces;

    #define MAK_hdr_idx_free_ptr MAK_base_md._hdr_idx_free_ptr
    ptr_t _hdr_idx_free_ptr;

    #define MAK_all_bottom_indices MAK_base_md._all_bottom_indices
    bottom_index * _all_bottom_indices;
                                /* Pointer to first (lowest addr) */
                                /* bottom_index.                  */
    
    #define MAK_all_bottom_indices_end MAK_base_md._all_bottom_indices_end
    bottom_index * _all_bottom_indices_end;
                                /* Pointer to last (highest addr) */
                                /* bottom_index.                  */

    /* persistent logs */

    #define MAK_persistent_log_version  MAK_arrays._persistent_log_version    
    unsigned long _persistent_log_version;
    
    #define MAK_persistent_log_start MAK_arrays._persistent_log_start
    ptr_t _persistent_log_start;

    #define MAK_persistent_initialized MAK_arrays._persistent_initialized
    int _persistent_initialized;
   
    
};








#endif


