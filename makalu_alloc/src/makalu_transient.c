#include "makalu_internal.h"

GC_INNER ptr_t GC_transient_scratch_alloc(size_t bytes)
{
    register ptr_t result = GC_transient_scratch_free_ptr;

    bytes += GRANULE_BYTES-1;
    bytes &= ~(GRANULE_BYTES-1);
    GC_transient_scratch_free_ptr += bytes;
    if (GC_transient_scratch_free_ptr <=GC_transient_scratch_end_ptr) {
        return(result);
    }
    {
        word bytes_to_get = MINHINCR * HBLKSIZE;

        if (bytes_to_get <= bytes) {
          /* Undo the damage, and get memory directly */
            bytes_to_get = bytes;
#           ifdef USE_MMAP
                bytes_to_get += GC_page_size - 1;
                bytes_to_get &= ~(GC_page_size - 1);
#           endif
            result = (ptr_t)GET_MEM(bytes_to_get);
            GC_add_to_our_memory(result, bytes_to_get);
            GC_transient_scratch_free_ptr -= bytes;
            GC_transient_scratch_last_end_ptr = result + bytes;
            return(result);
        }
        result = (ptr_t)GET_MEM(bytes_to_get);
        GC_add_to_our_memory(result, bytes_to_get);
        if (result == 0) {
            if (GC_print_stats)
                GC_log_printf("Out of memory - trying to allocate less\n");
            GC_transient_scratch_free_ptr -= bytes;
            bytes_to_get = bytes;
#           ifdef USE_MMAP
                bytes_to_get += GC_page_size - 1;
                bytes_to_get &= ~(GC_page_size - 1);
#           endif
            result = (ptr_t)GET_MEM(bytes_to_get);
            GC_add_to_our_memory(result, bytes_to_get);
            return result;
        }
        GC_transient_scratch_free_ptr = result;
        GC_transient_scratch_end_ptr = GC_transient_scratch_free_ptr + bytes_to_get;
        GC_transient_scratch_last_end_ptr = GC_transient_scratch_end_ptr;
        return(GC_transient_scratch_alloc(bytes));
    }
}

