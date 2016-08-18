#include "makalu.h"

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

static int FD = 0;
static char *base_addr = NULL;
static char *curr_addr = NULL;


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
    base_addr = (char*) addr;
    //adress to remap to, the root pointer to gc metadata, 
    //and the curr pointer at the end of the day
    curr_addr = (char*) ((size_t)addr + 3 * sizeof(intptr_t));
    printf("Addr: %p\n", addr);
    printf("Base_addr: %p\n", base_addr);
    printf("Current_addr: %p\n", curr_addr);
}

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



void __store_heap_root(void* root)
{
     *(((intptr_t*) base_addr) + 2) = (intptr_t) root;
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
       printf("\n----Ran out of space in mmaped file-----\n");
       return 1;
    }
    curr_addr = next;
    *memptr = res;
    //printf("Current NVM Region Addr: %p\n", curr_addr);

    return 0;
}



int main(){

    __map_transient_region();
    void* ret = MAK_start(&__nvm_region_allocator);
    int i;
    for (i=0; i < 10; i++) {
        void* p = MAK_malloc(sizeof(int));
        printf("Allocated address: %p\n", p);
    }
    return 0;
}
