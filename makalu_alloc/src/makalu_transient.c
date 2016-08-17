#include "makalu_internal.h"
#include <sys/mman.h>
#include <fcntl.h>

MAK_INNER struct _MAK_transient_metadata MAK_transient_md = {0};

MAK_INNER ptr_t MAK_get_transient_memory(word bytes)
{
    void* result;
    if (bytes & (MAK_page_size - 1)) 
        ABORT("Bad GET_MEM arg");
    
    result = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (result == MAP_FAILED)
        ABORT("Transient scratch space: mmap failed"); 
    
    return (ptr_t) result;    
}

MAK_INNER ptr_t MAK_transient_scratch_alloc(size_t bytes)
{
    register ptr_t result = MAK_transient_scratch_free_ptr;

    bytes += GRANULE_BYTES-1;
    bytes &= ~(GRANULE_BYTES-1);
    MAK_transient_scratch_free_ptr += bytes;
    if (MAK_transient_scratch_free_ptr <= MAK_transient_scratch_end_ptr) {
        return(result);
    }
    {
        word bytes_to_get = MINHINCR * HBLKSIZE;

        if (bytes_to_get <= bytes) {
          /* Undo the damage, and get memory directly */
            bytes_to_get = bytes;
            result = (ptr_t)GET_MEM(bytes_to_get);
            MAK_transient_scratch_free_ptr -= bytes;
            MAK_transient_scratch_last_end_ptr = result + bytes;
            return(result);
        }
        result = (ptr_t)GET_MEM(bytes_to_get);
        if (result == 0) {
            MAK_transient_scratch_free_ptr -= bytes;
            bytes_to_get = bytes;
            result = (ptr_t)GET_MEM(bytes_to_get);
            return result;
        }
        MAK_transient_scratch_free_ptr = result;
        MAK_transient_scratch_end_ptr = MAK_transient_scratch_free_ptr + bytes_to_get;
        MAK_transient_scratch_last_end_ptr = MAK_transient_scratch_end_ptr;
        return(MAK_transient_scratch_alloc(bytes));
    }
}
