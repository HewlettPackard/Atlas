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
 */

#ifndef _REGION_MANAGER_H
#define _REGION_MANAGER_H

#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define FILESIZE 5*1024*1024*1024ULL + 24
#define HEAPFILE "/dev/shm/gc_heap.dat"

#define CLFLUSH(addr) \
   { \
       __asm__ __volatile__ ("mfence" ::: "memory"); \
       __asm__ __volatile__ (   \
       "clflush %0 \n" : "+m" (*(char*)(addr))  \
       ); \
       __asm__ __volatile__ ("mfence" ::: "memory"); \
}


#ifdef __cplusplus
extern "C" {
#endif

    //mmap anynomous
    void __map_transient_region();

    //mmap file
    void __map_persistent_region();
    void __remap_persistent_region();

    //persist the curr and base address
    void __close_persistent_region();

    //print the status
    void __close_transient_region();

    //store heap root
    void __store_heap_start(void*);

    //retrieve heap root
    void* __fetch_heap_start();

    int __nvm_region_allocator(void** /*ret */, size_t /* alignment */, size_t /*size */);


#ifdef __cplusplus
}
#endif

#endif //_REGION_MANAGER_H
