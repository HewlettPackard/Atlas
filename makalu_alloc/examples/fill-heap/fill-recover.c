#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>

#include "region_manager.h"
#include "makalu.h"


int main(int argc, char *argv[])
{

    __remap_persistent_region();
    void* hstart = __fetch_heap_start();

    MAK_start_off(hstart, &__nvm_region_allocator);
    if (MAK_collect_off())
        printf("Successful GC offline\n");
    
    MAK_close();

    __close_persistent_region();
    return 0;    
}
