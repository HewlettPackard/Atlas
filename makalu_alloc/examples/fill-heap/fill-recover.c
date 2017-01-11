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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>

#include "region_manager.h"
#include "makalu.h"

/* Fill-recover recovers and  */
/* collects crash induced leaks */

int main(int argc, char *argv[])
{

    __remap_persistent_region();
    void* hstart = __fetch_heap_start();
    printf("Heap start address: %p\n", hstart);

    MAK_start_off(hstart, &__nvm_region_allocator);
    if (MAK_collect_off())
        printf("Successful GC offline\n");
    
    MAK_close();

    __close_persistent_region();
    return 0;    
}
