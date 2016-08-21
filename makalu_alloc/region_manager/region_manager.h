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
