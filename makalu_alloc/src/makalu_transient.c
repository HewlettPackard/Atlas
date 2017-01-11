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
 *
 * Source code is partially derived from Boehm-Demers-Weiser Garbage 
 * Collector (BDWGC) version 7.2 (license is attached)
 *
 * File:
 *   headers.c
 *
 *   Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 *   Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
 *   Copyright (c) 1996 by Silicon Graphics.  All rights reserved.
 *
 *   THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY EXPRESSED
 *   OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.
 *
 *   Permission is hereby granted to use or copy this program
 *   for any purpose,  provided the above notices are retained on all copies.
 *   Permission to modify the code and to distribute modified code is granted,
 *   provided the above notices are retained, and a notice that the code was
 *   modified is included with the above copyright notice.
 */

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
