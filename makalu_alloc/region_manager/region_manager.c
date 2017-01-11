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

#include "region_manager.h"

static int FD = 0;
static char *base_addr = NULL;
static char *curr_addr = NULL;

void __map_transient_region()
{
    char* ret = (char*) mmap((void*) 0, FILESIZE,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS,
                       -1, 0);
    if (ret == MAP_FAILED){
        printf ("Mmap failed");
        exit(1);
    }
    base_addr = ret;
    curr_addr = (char*) ((size_t) ret + 3 * sizeof(intptr_t));                    
    printf("Addr: %p\n", ret);
    printf("Base_addr: %p\n", base_addr);
    printf("Current_addr: %p\n", curr_addr);
}

void __map_persistent_region(){
    int fd;
    fd  = open(HEAPFILE, O_RDWR | O_CREAT | O_TRUNC,
                  S_IRUSR | S_IWUSR);

    FD = fd;
    off_t offt = lseek(fd, FILESIZE-1, SEEK_SET);
    assert(offt != -1);

    int result = write(fd, "", 1);
    assert(result != -1);

    void * addr =
        mmap(0, FILESIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(addr != MAP_FAILED);

    *((intptr_t*)addr) = (intptr_t) addr;
    CLFLUSH(addr);
    base_addr = (char*) addr;
    //adress to remap to, the root pointer to gc metadata, 
    //and the curr pointer at the end of the day
    curr_addr = (char*) ((size_t)addr + 3 * sizeof(intptr_t));
    *(((intptr_t*) base_addr) + 1) = (intptr_t) curr_addr;
    CLFLUSH( (((intptr_t*) base_addr) + 1)); 
    printf("Addr: %p\n", addr);
    printf("Base_addr: %p\n", base_addr);
    printf("Current_addr: %p\n", curr_addr);
}

void __remap_persistent_region()
{
         int fd;
     fd  = open(HEAPFILE, O_RDWR,
                  S_IRUSR | S_IWUSR);

    FD = fd;
    off_t offt = lseek(fd, FILESIZE-1, SEEK_SET);
    assert(offt != -1);

    int result = write(fd, "", 1);
    assert(result != -1);

    offt = lseek(fd, 0, SEEK_SET);
    assert (offt == 0);
    intptr_t forced_addr;

    int bytes_read = read (fd, &forced_addr, sizeof(intptr_t));
    if (bytes_read <= 0)
    {
        printf("Something went wrong when trying to retrieve the forced address.\n");
    }

    void * addr =
        mmap((void*)forced_addr, FILESIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(addr != MAP_FAILED);
    assert(forced_addr == (intptr_t) addr);

    base_addr = (char*) addr;
    curr_addr = (char*) (*(((intptr_t*) base_addr) + 1));
    printf("Forced Addr: %p\n", (void*) forced_addr);
    printf("Addr: %p\n", addr);
    printf("Base_addr: %p\n", base_addr);
    printf("Curr_addr: %p\n", curr_addr);
}

void __close_persistent_region()
{
    *(((intptr_t*) base_addr) + 1) = (intptr_t) curr_addr;
    CLFLUSH( (((intptr_t*) base_addr) + 1)); 

    printf("At the end current addr: %p\n", curr_addr);

    unsigned long space_used = ((unsigned long) curr_addr 
         - (unsigned long) base_addr);
    unsigned long remaining_space = 
         ((unsigned long) FILESIZE - space_used) / (1024 * 1024);
    printf("Space Used(rounded down to MiB): %ld, Remaining(MiB): %ld\n", 
               space_used / (1024 * 1024), remaining_space);
    close(FD);
}

void __close_transient_region()
{
    printf("At the end current addr: %p\n", curr_addr);
    unsigned long space_used = ((unsigned long) curr_addr
         - (unsigned long) base_addr);
    unsigned long remaining_space =
         ((unsigned long) FILESIZE - space_used) / (1024 * 1024);
    printf("Space Used(rounded down to MiB): %ld, Remaining(MiB): %ld\n",
               space_used / (1024 * 1024), remaining_space);
}

void __store_heap_start(void* root)
{
     *(((intptr_t*) base_addr) + 2) = (intptr_t) root;
    CLFLUSH( (((intptr_t*) base_addr) + 2)); 
}

void* __fetch_heap_start()
{
    return (void*) (*(((intptr_t*) base_addr) + 2));
}


int __nvm_region_allocator(void** memptr, size_t alignment, size_t size)
{
    char* next;
    char* res;
    if (size < 0) return 1;
  
    if (((alignment & (~alignment + 1)) != alignment)  ||    //should be multiple of 2
        (alignment < sizeof(void*))) return 1; //should be atleast the size of void*
    size_t aln_adj = (size_t) curr_addr & (alignment - 1);
    
    if (aln_adj != 0)
       curr_addr += (alignment - aln_adj);
 
    res = curr_addr;
    next = curr_addr + size;
    if (next > base_addr + FILESIZE){
       printf("\n----Region Manager: out of space in mmaped file-----\n");
       return 1;
    }
    curr_addr = next;
    *(((intptr_t*) base_addr) + 1) = (intptr_t) curr_addr;
    CLFLUSH( (((intptr_t*) base_addr) + 1)); 
    *memptr = res;
 
    return 0;
}

#if 0
int main()
{
    __remap_persistent_region();
    void* ptr = NULL;
    __nvm_region_allocator(&ptr, 4096, 64); 
    printf("Allocated addr: %p\n", ptr);
    __close_persistent_region(); 
    return 0;
}
#endif
