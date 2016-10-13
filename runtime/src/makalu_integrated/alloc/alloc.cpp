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
 

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <assert.h>
#include <string.h>
#include <sys/file.h>
#include <errno.h>

#include "atlas_api.h"
#include "atlas_alloc.h"
#include "atlas_alloc_priv.h"
#include "region_internal.h"
#include "util.h"
#include "defer_free.h"

#ifdef PMM_OS
void *pmm_mmap_addr=NULL;


int  NVM_GetRegionSize()
{
         return FILESIZE;
}
#endif

uint32_t Curr_Rid = 0;

// once it is set (before threads are spawned), it is readonly
void *region_table_addr = 0;

void *log_base_addr = (void*)0x301000000000;

MapInterval * volatile map_interval_p = 0;

// Region table file descriptor
int region_table_fd;

// table of locks for heaps
pthread_mutex_t AllocLockTab[MAX_NUM_HEAPS];

// common attribute for the table of locks
pthread_mutexattr_t alloc_mutex_attr;

pthread_mutex_t global_heap_lock;

// Currently, two pieces of information, each of type size_t are maintained
size_t NVM_get_metadata_size()
{
    return 2*sizeof(size_t);
}

size_t NVM_get_actual_alloc_size(size_t sz)
{
    return (sz + NVM_get_metadata_size() + NVM_ALIGN_MASK) & ~NVM_ALIGN_MASK;
}

size_t NVM_get_requested_alloc_size_from_mem(void *mem)
{
    return *((size_t*)mem);
}

size_t NVM_get_requested_alloc_size_from_ptr(void *ptr)
{
    void * mem = ptr2mem(ptr);
    return *((size_t*)mem);
}

size_t *NVM_get_is_allocated_ptr_from_mem(void *mem)
{
    return (size_t*)((char*)mem + sizeof(size_t));
}
    
size_t *NVM_get_is_allocated_ptr_from_ptr(void *ptr)
{
    void *mem = ptr2mem(ptr);
    return (size_t*)((char*)mem + sizeof(size_t));
}

bool NVM_Is_Mem_Allocated(void *mem)
{
    return *NVM_get_is_allocated_ptr_from_mem(mem) == true;
}

bool NVM_Is_Ptr_Allocated(void *ptr)
{
    return *NVM_get_is_allocated_ptr_from_ptr(ptr) == true;
}

void NVM_Set_Mem_Is_Allocated(void *mem, bool b)
{
    size_t *elem_ptr = NVM_get_is_allocated_ptr_from_mem(mem);
    NVM_STR2(*elem_ptr, b, sizeof(*elem_ptr)*8);
}

void NVM_Set_Ptr_Is_Allocated(void *ptr, bool b)
{
    size_t *elem_ptr = NVM_get_is_allocated_ptr_from_ptr(ptr);
    NVM_STR2(*elem_ptr, b, sizeof(*elem_ptr)*8);
}

void NVM_Set_Ptr_Is_Allocated(void * ptr, bool b, bool do_not_log)
{
    size_t * elem_ptr = NVM_get_is_allocated_ptr_from_ptr(ptr);
    *elem_ptr = b;
    NVM_FLUSH((void*)elem_ptr);
}

void * NVM_Get_Region_Data_Ptr(HeapEntry * rgn_entry)
{
    return mem2ptr((char *)(rgn_entry->base_addr) +
                   NVM_get_actual_alloc_size(sizeof(intptr_t)));
}

size_t get_next_bin_number(size_t bin_number)
{
    assert(bin_number); // 0 should have been filtered out
    assert(!(bin_number % NVM_ALLOC_ALIGNMENT)); // multiple of alignment
    return bin_number + NVM_ALLOC_ALIGNMENT;
}

bool is_cache_line_aligned(void *p)
{
    return ((uintptr_t)p & cache_line_mask) == (uintptr_t)p;
}

void * nvm_alloc(size_t sz,
                 intptr_t **start_addr_ptr,
                 intptr_t *base_addr,
                 FreeList *free_list,
                 HeapEntry *region_entry)
{
    // Lock should already be held at this point
    size_t alloc_sz = NVM_get_actual_alloc_size(sz);
    char *start_addr = (char *)(*start_addr_ptr);

    if ((start_addr+alloc_sz) > ((char*)base_addr + FILESIZE))
        return nvm_alloc_from_free_list(sz, free_list, region_entry);
    
    void *ret = (void*)(start_addr + NVM_get_metadata_size());

    // Set the metadata
    *((size_t*)start_addr) = sz;

    // TODO the following should work since the lock is of recursive type
    // Can't call the following since it calls CreateLogEntry which calls
    // nvm_alloc again. Not logging is ok since if there is a rollback,
    // this location should be unreachable and should be reclaimed by GC.
//    NVM_Set_Mem_Is_Allocated(start_addr, true);
    
    *((size_t*)(start_addr + sizeof(size_t))) = true; // allocated now

    // If a crash happens here, the memory appears allocated but it is not
    // assigned to any program-visible entity, hence it is essentially lost.
    // The recovery phase has to identify this situation and reclaim the
    // memory.
    
    *start_addr_ptr = (intptr_t*) (start_addr + alloc_sz);
    return ret;
}


/* This method is primarily called by the GC memory allocator */
/* to expand the heap within the current NVM region */
int NVM_Memalign(void** memptr, size_t alignment, size_t size){
    AcquireRegionAllocLock(Curr_Rid);
    assert(Curr_Rid < NVM_GetNumRegions());
    HeapEntry * region_data = NVM_GetRegionMetaData(Curr_Rid);
    //fprintf(stderr, "NVM_Memalign called %d: %s\n",
      //          Curr_Rid, region_data->name);

    if (!region_data->is_valid)
    {
        fprintf(stderr, "[Atlas-pheap] Invalid region %d: %s\n",
                Curr_Rid, region_data->name);
        PrintBackTrace();
        assert(region_data->is_valid);
    }
    char *curr_addr = (char*) region_data -> curr_alloc_addr;
    char *base_addr = (char*) region_data -> base_addr;
    char *next;
    char *res;
    if (size < 0) {
        ReleaseRegionAllocLock(Curr_Rid);
        return 1;
    }
    
    if (((alignment & (~alignment + 1)) != alignment)  ||    //should be multiple of 2
       (alignment < sizeof(void*))){
         ReleaseRegionAllocLock(Curr_Rid);
         return 1; //should be atleast the size of void*
    }

    size_t aln_adj = (size_t) curr_addr & (alignment - 1);
    if (aln_adj != 0)
        curr_addr += (alignment - aln_adj);

    res = curr_addr;
    next = curr_addr + size;
    if (next > base_addr + FILESIZE){
         fprintf(stderr, "No more memory to allocate within the NVM region %d: %s\n",
                Curr_Rid, region_data->name);
        ReleaseRegionAllocLock(Curr_Rid);
        return 1;
    }
    region_data -> curr_alloc_addr = (intptr_t*) next;

    ReleaseRegionAllocLock(Curr_Rid);
    *memptr = (void*) res;
    return 0;    
}

// This is a public interface
void * nvm_alloc(size_t sz, uint32_t rid)
{
    assert(rid == 0);
    AcquireRegionAllocLock(rid);
    
    assert(rid < NVM_GetNumRegions());
    HeapEntry * region_data = NVM_GetRegionMetaData(rid);

    if (!region_data->is_valid)
    {
        fprintf(stderr, "[Atlas-pheap] Invalid region %d: %s\n",
                rid, region_data->name);
        PrintBackTrace();
        assert(region_data->is_valid);
    }

    void * ret = nvm_alloc(sz, &(region_data->curr_alloc_addr),
                           region_data->base_addr,
                           region_data->free_list,
                           region_data);
    
    ReleaseRegionAllocLock(rid);

    return ret;
}

void *nvm_alloc_cache_line_aligned(size_t sz, uint32_t rid)
{
    assert(rid == 0);
    AcquireRegionAllocLock(rid);

    // TODO use uintptr_t
    assert(rid < NVM_GetNumRegions());
    HeapEntry *region_data = NVM_GetRegionMetaData(rid);

    if (!region_data->is_valid)
    {
        fprintf(stderr, "[Atlas-pheap] Invalid region %d: %s\n",
                rid, region_data->name);
        PrintBackTrace();
        assert(region_data->is_valid);
    }

    intptr_t curr_alloc_addr = (intptr_t)region_data->curr_alloc_addr;
    intptr_t cache_line = (intptr_t)curr_alloc_addr & cache_line_mask;
    intptr_t next_cache_line = cache_line + cache_line_size;

    if (curr_alloc_addr == next_cache_line - (intptr_t)NVM_get_metadata_size())
    {
        void *ret = nvm_alloc(sz, rid);
        ReleaseRegionAllocLock(rid);
        assert(is_cache_line_aligned(ret));
        return ret;
    }
    else
    {
        size_t min_size = NVM_get_actual_alloc_size(1);
        intptr_t diff = next_cache_line - NVM_get_metadata_size() - curr_alloc_addr;
        if (diff < (intptr_t)min_size) diff += cache_line_size;
        size_t request_size = diff - NVM_get_metadata_size() - NVM_ALIGN_MASK;

        void *tmp = nvm_alloc(request_size, rid);
        void *ret = nvm_alloc(sz, rid);
        nvm_free(tmp, true);
        ReleaseRegionAllocLock(rid);
        assert(is_cache_line_aligned(ret));
        return ret;
    }
}

// This is a public interface
void *nvm_calloc(size_t nmemb, size_t sz, uint32_t rid)
{
    void *ret = nvm_alloc(nmemb*sz, rid);
    memset(ret, 0, nmemb*sz);
    return ret;
}

// This is a public interface
// TODO a more optimized implementation. Currently, the area pointed
// to by ptr is always moved, this should change.
void *nvm_realloc(void *ptr, size_t sz, uint32_t rid)
{
    if (!ptr && sz) return nvm_alloc(sz, rid);
    if (!sz)
    {
        nvm_free(ptr);
        return 0;
    }
    size_t curr_sz = NVM_get_requested_alloc_size_from_ptr(ptr);
    void *ret = nvm_alloc(sz, rid);
    memcpy(ret, ptr, curr_sz < sz ? curr_sz : sz);
    nvm_free(ptr);
    return ret;
}

void * nvm_alloc_from_free_list(size_t sz, FreeList *free_list,
                                HeapEntry *region_entry)
{
    assert(free_list);
    
    FreeList & fl = *free_list;

    size_t bin_number = (sz < MAX_FREE_CATEGORY) ? 
        (sz + NVM_ALIGN_MASK) & ~NVM_ALIGN_MASK : MAX_FREE_CATEGORY;

    while (true)
    {
        FreeList::iterator ci = fl.find(bin_number);
        if (ci != fl.end())
        {
            MemMap & mem_map = ci->second;

            // Allocate from the first entry
            MemMap::iterator mem_ci_end = mem_map.end();
            MemMap::iterator mem_ci = mem_map.begin();
            if (mem_ci != mem_ci_end)
            {
                char * mem = (char *)mem_ci->first;
                assert(!NVM_Is_Mem_Allocated(mem));

                void * ret = (void *)(mem + NVM_get_metadata_size());
                *((size_t *)mem) = sz;
                *((size_t *)(mem + sizeof(size_t))) = true;

                assert(NVM_Is_Mem_Allocated(mem));

                mem_map.erase(mem_ci);

                return ret;
            }
            // else nothing to do, bin is empty
        }
        // else nothing to do, this bin was never created

        if (bin_number == MAX_FREE_CATEGORY)
        {
            pair<void*,bool> res = BuildPartialFreeList(sz, region_entry);
            if (!res.second)
            {
                PrintBackTrace();
                fprintf(stderr, "Out of persistent memory in region %s\n",
                        region_entry->name);
                assert(0);
            }
            return res.first;
        }
        bin_number = get_next_bin_number(bin_number);
    }
}

// While a redesign is required, we can get away without holding a lock
// during free if we add to the free list elsewhere. This addition
// can be done during alloc in an incremental manner
// (see BuildPartialFreeList). 
void nvm_free(void * p)
{
    pair<bool, uint32_t> res = NVM_GetRegionId(p, 1 /* dummy */);
    if (!res.first)
    {
        free(p);
        return;
    }

    if (!NVM_Is_Ptr_Allocated(p))
    {
        fprintf(stderr, "[Atlas-pheap] assert: %p %ld %ld\n",
                (size_t *)p, *((size_t *)p),
                *(size_t *)((char *)p+sizeof(size_t)));
        assert(NVM_Is_Ptr_Allocated(p) && "free called on unallocated memory");
    }

    NVM_Set_Ptr_Is_Allocated(p, false);
}

void nvm_free(void * p, bool do_not_log)
{
    if (!NVM_Is_Ptr_Allocated(p))
    {
        fprintf(stderr, "[Atlas-pheap] assert: %p %ld %ld\n",
                (size_t *)p, *((size_t *)p),
                *(size_t *)((char *)p+sizeof(size_t)));
        assert(NVM_Is_Ptr_Allocated(p) && "free called on unallocated memory");
    }

    NVM_Set_Ptr_Is_Allocated(p, false, do_not_log);
}

uint32_t NVM_CreateRegion(const char *name, int flags)
{
    uint32_t rid = NVM_CreateRegion_priv(name, flags);
    Curr_Rid = rid;
    //KUMUD_TODO: start persistent heap metadata online
    //GC_set_collect_offline_only(1);
    void* gc_base = GC_INIT_PERSISTENT(&NVM_Memalign);
    NVM_SetRegionRoot(rid, gc_base);
    GC_set_defer_free_fn(&DeferFreeCallback);
    return rid;
}

#ifdef PMM_OS
uint32_t NVM_CreateRegion_priv(const char *name, int flags,
                                bool is_lock_held, void *forced_base_addr)
{
    int status;
    assert(strlen(name) <= MAXLEN);

    // Mediate threads within a process
    if (!is_lock_held)
    {
        status = pthread_mutex_lock(&global_heap_lock);
        assert(!status);
    }

    assert(!NVM_FindRegionMetaData(name).first &&
           "Region %s already exists, use a different region");

    // Ensure no other region is flocked until this flock is released
    // Why so? TODO.
    int flock_status = flock(region_table_fd, LOCK_EX);
    assert(!flock_status);

    uint32_t num_entries = NVM_GetNumRegions();
    HeapEntry * region_entry = NVM_GetRegionMetaData(num_entries);
    assert(region_entry);
    region_entry->file_desc =
        NVM_CreateNewMappedFile(name, flags, forced_base_addr);
    forced_base_addr=pmm_mmap_addr;

    strcpy(region_entry->name, name);
    region_entry->id = num_entries;
    region_entry->base_addr = (intptr_t *)forced_base_addr;
    region_entry->curr_alloc_addr = region_entry->base_addr;
    region_entry->free_list = new FreeList;
    region_entry->is_mapped = region_entry->is_valid = true;

    nvm_barrier((void*)region_entry->name);
    nvm_barrier((void*)&(region_entry->id));
    if (isOnDifferentCacheLine(&region_entry->id, &region_entry->is_valid))
        nvm_barrier((void*)&region_entry->is_valid);

    // The following flushes as well
    NVM_SetNumRegions(num_entries + 1);

    flock_status = flock(region_table_fd, LOCK_UN);
    assert(!flock_status);

    // Need OS support to reclaim regions if a crash happens during or
    // just after mapping a file
    // We write the file_desc to the region table entry after releasing
    // the flock on the region table. But this is ok since no other entity
    // (thread or process) can write to the same location.
    // No need to flush the file_desc since it is useless after process
    // termination.

    // bug? TODO.
    // The root pointer must be set properly before setting the num_entries.
    // otherwise an intervening failure would cause a later GetRoot to fail.
    
    // root_ptr is at a known offset
    intptr_t * root_ptr = (intptr_t *) nvm_alloc(
        sizeof(intptr_t), &(region_entry->curr_alloc_addr),
        region_entry->base_addr, region_entry->free_list, region_entry);
    *root_ptr = 0;
    nvm_barrier((void*)root_ptr);

    NVM_CowAddToMapInterval(&map_interval_p,
                            (uint64_t)forced_base_addr,
                            (uint64_t)((char*)forced_base_addr+FILESIZE),
                            num_entries);

    if (!is_lock_held)
    {
        status = pthread_mutex_unlock(&global_heap_lock);
        assert(!status);
    }

    AtlasTrace(stderr, "[Atlas-pheap] CreateRegion created region name=%s id %d\n",
               name, num_entries);
    RegionDump(region_entry);


    return num_entries; // ids are 0-based
}
#else
uint32_t NVM_CreateRegion_priv(const char *name, int flags,
                               bool is_lock_held, void *forced_base_addr)
{
    int status;
    assert(strlen(name) <= MAXLEN);

    // Mediate threads within a process
    if (!is_lock_held)
    {
        status = pthread_mutex_lock(&global_heap_lock);
        assert(!status);
    }

    assert(!NVM_FindRegionMetaData(name).first &&
           "Region %s already exists, use a different region");

    // Ensure no other region is flocked until this flock is released
    // Why so? TODO.
    int flock_status = flock(region_table_fd, LOCK_EX);
    assert(!flock_status);

    if (!forced_base_addr) forced_base_addr = NVM_ComputeNewBaseAddr();
    
    uint32_t num_entries = NVM_GetNumRegions();
    HeapEntry * region_entry = NVM_GetRegionMetaData(num_entries);
    assert(region_entry);

    strcpy(region_entry->name, name);
    region_entry->id = num_entries;
    region_entry->base_addr = (intptr_t *)forced_base_addr;
    region_entry->curr_alloc_addr = region_entry->base_addr;
    region_entry->free_list = new FreeList;
    region_entry->is_mapped = region_entry->is_valid = true;
    
    NVM_FLUSH((void*)region_entry->name);
    NVM_FLUSH((void*)&(region_entry->id));
    if (isOnDifferentCacheLine(&region_entry->id, &region_entry->is_valid))
        NVM_FLUSH((void*)&region_entry->is_valid);

    // The following flushes as well
    NVM_SetNumRegions(num_entries + 1);
    
    flock_status = flock(region_table_fd, LOCK_UN);
    assert(!flock_status);

    // TODO write down which lock is protecting what and the rules that
    // must be followed.
    // Why is file_desc set after releasing the flock on region_table_fd?
    
    // Need OS support to reclaim regions if a crash happens during or
    // just after mapping a file
    // We write the file_desc to the region table entry after releasing
    // the flock on the region table. But this is ok since no other entity
    // (thread or process) can write to the same location.
    // No need to flush the file_desc since it is useless after process
    // termination.
    region_entry->file_desc =
        NVM_CreateNewMappedFile(name, flags, forced_base_addr);

    // bug? TODO
    // The root pointer must be set properly before setting the num_entries.
    // otherwise an intervening failure might have the root corrupted.
    
    // root_ptr is at a known offset
    intptr_t * root_ptr = (intptr_t *) nvm_alloc(
        sizeof(intptr_t), &(region_entry->curr_alloc_addr),
        region_entry->base_addr, region_entry->free_list, region_entry);
    *root_ptr = 0;
    NVM_FLUSH((void*)root_ptr);

    NVM_CowAddToMapInterval(&map_interval_p,
                            (uint64_t)forced_base_addr,
                            (uint64_t)((char*)forced_base_addr+FILESIZE),
                            num_entries);
    
    if (!is_lock_held)
    {
        status = pthread_mutex_unlock(&global_heap_lock);
        assert(!status);
    }

    AtlasTrace(stderr, "[Atlas-pheap] CreateRegion created region name=%s id %d\n",
               name, num_entries);
    RegionDump(region_entry);
    
    return num_entries; // ids are 0-based
}
#endif

uint32_t NVM_FindOrCreateRegion(const char *name, int flags, int *is_created_p)
{
    return NVM_FindOrCreateRegion_priv(name, flags, false, is_created_p);
}

// A log file is private to the process that creates it. At this point,
// concurrent runs of the same binary are not allowed. This is because
// of recovery complications if more than one of these processes fail.
uint32_t NVM_CreateLogFile(const char *name)
{
    assert(strlen(name) < MAXLEN+1);

    // Ensure no other region is flocked until this flock is released
    // Why so? TODO.
    int flock_status = flock(region_table_fd, LOCK_EX);
    assert(!flock_status);

    // No need to acquire a thread-level lock since the log file should be
    // created before threads are spawned
    bool is_incr = false;
    uint32_t num_entries = -1;
    HeapEntry *region_entry = NVM_FindRegionMetaData(name, true).first;
    if (region_entry)
    {
        // name and id remain unchanged
        region_entry->is_mapped = true;
        region_entry->is_valid = true;
    }
    else
    {
        is_incr = true;
        num_entries = NVM_GetNumRegions();
        region_entry = NVM_GetRegionMetaData(num_entries);
        assert(region_entry);

        strcpy(region_entry->name, name);
        region_entry->id = num_entries;
        region_entry->is_mapped = region_entry->is_valid = true;
    }
    region_entry->base_addr = (intptr_t*)log_base_addr;
    region_entry->curr_alloc_addr = region_entry->base_addr;
    if (is_incr) NVM_FLUSH((void*)region_entry->name);
    NVM_FLUSH((void*)&(region_entry->id));
    region_entry->free_list = new FreeList;
    if (isOnDifferentCacheLine(&region_entry->id, &region_entry->is_valid))
        NVM_FLUSH((void*)&region_entry->is_valid);
    if (is_incr) NVM_SetNumRegions(num_entries+1);

    flock_status = flock(region_table_fd, LOCK_UN);
    assert(!flock_status);

    NVM_CreateLogDir();
    char s[MAXLEN];
    NVM_qualifyPathName(s, name);

    int fd = open(s, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    assert(fd != -1);
    // We don't care about flushing out the following update since it
    // won't matter in another process
    region_entry->file_desc = fd;

#if !defined(NDEBUG)    
    off_t offt =
#endif
        lseek(fd, FILESIZE-1, SEEK_SET);
    assert(offt != -1);

#if !defined(NDEBUG)    
    int result =
#endif        
        write(fd, "", 1);
    assert(result != -1);

#if !defined(NDEBUG)    
    void *base_addr =
#endif        
        (intptr_t*)mmap(log_base_addr, FILESIZE,
                        PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
#if !defined(NDEBUG)    
    assert(base_addr != MAP_FAILED);
#endif    
#ifndef PMM_OS
    assert(base_addr == log_base_addr && "requested base address not obtained --- this is not a tolerated failure");
#endif

    // bug? TODO
    // The root pointer must be set properly before setting the num_entries.
    // Otherwise, an intervening failure might have the root corrupted.
    intptr_t * root_ptr = (intptr_t *) nvm_alloc(
        sizeof(intptr_t), &(region_entry->curr_alloc_addr),
        region_entry->base_addr, region_entry->free_list, region_entry);
    *root_ptr = 0;
    NVM_FLUSH((void*)root_ptr);

    NVM_CowAddToMapInterval(&map_interval_p,
                            (uint64_t)log_base_addr,
                            (uint64_t)((char*)log_base_addr+FILESIZE),
                            region_entry->id);
    
    AtlasTrace(stderr, "[Atlas-pheap] Log file %s created with id %d\n",
               name, region_entry->id);
    
    return region_entry->id;
}

uint32_t NVM_FindOrCreateRegion_priv(const char * name, int flags,
                                     bool is_forced_map, int *is_created_p)
{
    assert(strlen(name) <= MAXLEN);
    
    int status = pthread_mutex_lock(&global_heap_lock);
    assert(!status);
    
    pair<HeapEntry*,void*> res = NVM_FindRegionMetaData(name);
    HeapEntry * region_entry = res.first;
    if (region_entry)
    {
        assert(region_entry->is_valid);
        if (is_created_p) *is_created_p = false;
#ifdef PMM_OS
        if (region_entry->is_valid || is_forced_map)
//#elif !defined(_ALWAYS_MAP)
//        if (!region_entry->is_mapped || is_forced_map)
#endif
            // If a process has a mapped persistent heap when it crashes,
            // the attribute is_mapped will stay true. Ideally, the recovery
            // phase should detect this and set the is_mapped attribute to
            // false at the end of the recovery phase. For the most part,
            // this does occur today. However, since recovery detects a
            // region only when it undoes an update to it, it may not reset
            // the is_mapped attribute if it does not undo anything in that
            // region. (TODO: The correct implementation is to maintain
            // additional information about relevant regions in the log and
            // reset their attributes at the end of recovery.) As a
            // workaround, we will try to map a persistent heap during
            // normal execution unconditionally as below. If this routine
            // is called multiple times without unmapping in the middle,
            // it will be re-mapped unnecessarily but that should not be
            // a correctness problem since we are using the same base
            // address.
        {
            pair<void *, int> res =
                NVM_MapExistingFile(name, flags, region_entry->base_addr);

            if (region_entry->base_addr != res.first)
            {
                AtlasTrace(stderr,
                           "Error: region %s overlaps with existing one: ",
                           name);
                AtlasTrace(stderr, "requested %p, got back %p\n",
                           region_entry->base_addr,
                           res.first);
                assert(0);
            }

            NVM_CowAddToMapInterval(
                &map_interval_p,
                (uint64_t)region_entry->base_addr,
                (uint64_t)((char*)region_entry->base_addr+FILESIZE),
                region_entry->id);
            
            // Note that no flock is held on the region table but no other
            // entity should be accessing this entry either since there is
            // an existing flock on the corresponding persistent region.
            // BuildFreeList(region_entry);
            region_entry->free_list = new FreeList;
            region_entry->file_desc = res.second;
            region_entry->is_mapped = true;

            NVM_FLUSH((void*)&(region_entry->file_desc));
            if (isOnDifferentCacheLine(
                    &region_entry->file_desc, &region_entry->is_mapped))
                NVM_FLUSH((void*)&region_entry->is_mapped);
        }
        Curr_Rid = region_entry -> id;
        //KUMUD_TODO: restart persistent metadata online
        //GC_set_collect_offline_only(1);
        char* gc_base = (char*) NVM_GetRegionRoot(Curr_Rid);
        GC_RESTART_ONLINE(gc_base, &NVM_Memalign);
        GC_set_defer_free_fn(&DeferFreeCallback);
        status = pthread_mutex_unlock(&global_heap_lock);
        assert(!status);

        AtlasTrace(stderr,
                   "[Atlas-pheap] NVM_FindOrCreateRegion found region id %d\n",
                   region_entry->id);
        RegionDump(region_entry);
        
        return region_entry->id;
    }
    else
    {
        if (is_created_p) *is_created_p = true;
        uint32_t rid = NVM_CreateRegion_priv(name, flags, true, res.second);
        Curr_Rid = rid;
        //KUMUD_TODO: start persistent heap metadata online
        //GC_set_collect_offline_only(1);
        void* gc_base = GC_INIT_PERSISTENT(&NVM_Memalign); 
        NVM_SetRegionRoot(rid, gc_base);
        GC_set_defer_free_fn(&DeferFreeCallback); 
        status = pthread_mutex_unlock(&global_heap_lock);
        assert(!status);

        return rid;
    }
}

//KUMUD_TODO: add a flag to findregion to distinguish offline or online start
uint32_t NVM_FindRegion(const char *name, int flags)
{
    return NVM_FindRegion_priv(name, flags, false /*is_recover*/, true /* gc_alloc*/);
}

// TODO this is called from recover phase, so that is creating 2 different
// mappers!
uint32_t NVM_FindRegion_priv(const char *name, int flags, 
       bool is_recover, bool gc_alloc, bool is_forced_map)
{
    assert(strlen(name) <= MAXLEN);
    
    int status = pthread_mutex_lock(&global_heap_lock);
    assert(!status);

    // this interface expects an existing region
    HeapEntry * region_entry = NVM_FindRegionMetaData(name).first;
    assert(region_entry); 
    assert(region_entry->is_valid);
#ifdef PMM_OS
    if (region_entry->is_valid || is_forced_map)
#else
    if (!region_entry->is_mapped || is_forced_map)
#endif
    {
        pair<void *, int> res =
            NVM_MapExistingFile(name, flags, region_entry->base_addr);

        NVM_CowAddToMapInterval(
            &map_interval_p,
            (uint64_t)region_entry->base_addr,
            (uint64_t)((char*)region_entry->base_addr+FILESIZE),
            region_entry->id);

        assert(region_entry->base_addr == res.first);

        // Note that no flock is held on the region table but no other
        // entity should be accessing this entry either since there is
        // an existing flock on the corresponding persistent region.
//        BuildFreeList(region_entry);
        region_entry->free_list = new FreeList;
        region_entry->file_desc = res.second;
        region_entry->is_mapped = true;

        NVM_FLUSH((void*)&(region_entry->file_desc));
        if (isOnDifferentCacheLine(
                &region_entry->file_desc, &region_entry->is_mapped))
            NVM_FLUSH((void*)&region_entry->is_mapped);
    }
    if (gc_alloc){
        Curr_Rid = region_entry -> id;
        //GC_set_collect_offline_only(1);
        char* gc_base = (char*) NVM_GetRegionRoot(Curr_Rid);
        if (is_recover){
            GC_RESTART_OFFLINE(gc_base, &NVM_Memalign);
        }
        else {
            GC_RESTART_ONLINE(gc_base, &NVM_Memalign);
            GC_set_defer_free_fn(&DeferFreeCallback);
        }
    }
    status = pthread_mutex_unlock(&global_heap_lock);
    assert(!status);

    return region_entry->id;
}

// TODO flush valid and mapped bits
// TODO Call NVM_CowDeleteFromMapInterval under a lock
void NVM_CloseRegion(uint32_t rid)
{
    NVM_CloseRegion_priv(rid);
}

void NVM_CloseRegion_priv(uint32_t rid, bool is_recover)
{
    HeapEntry *he = NVM_GetRegionMetaData(rid);
    assert(he);
    assert(he->is_valid);
    GC_close();
    NVM_UnmapUnlockAndClose(he, is_recover);
}

void Unmap(HeapEntry *he)
{
    assert(he);
    assert(he->is_mapped);
#if !defined(NDEBUG)
    int status =
#endif        
        munmap(he->base_addr, FILESIZE);
    assert(status != -1);
    he->is_mapped = false;
}

void Unlock(HeapEntry *he)
{
#if !defined(NDEBUG)    
    int flock_status =
#endif        
        flock(he->file_desc, LOCK_UN);
    assert(!flock_status);
}

void NVM_UnmapAndClose(HeapEntry * he, bool is_recover)
{
    if (!is_recover)
    {
        delete(he->free_list);
        he->free_list = 0;
    }
    Unmap(he);
    close(he->file_desc);
}

void NVM_UnmapUnlockAndClose(HeapEntry * he, bool is_recover)
{
    if (!is_recover)
    {
        delete(he->free_list);
        he->free_list = 0;
    }
    Unmap(he);
    Unlock(he);
    close(he->file_desc);
}

void NVM_DeleteRegion(const char * name)
{
    // TODO should protect the region table entry or does it not matter?
    HeapEntry * he = NVM_FindRegionMetaData(name).first;
    assert(he);
    assert(he->is_valid);

    // Since we are deleting the file, do not unlock it at all lest another
    // process grabs it before we unlink. Let the unlink do the job of
    // unlock as well.
    if (he->is_mapped) NVM_UnmapAndClose(he);

    // for now, we are not compacting the region table. We still keep
    // the entry but change its validity
    he->is_valid = false;
    
    char s[MAXLEN];
    NVM_qualifyPathName(s, name);
    unlink(s);
}

pair<void *, int> NVM_MapExistingFile(
    const char * name, int flags, intptr_t * base_addr)
{
    char s[MAXLEN];
    NVM_qualifyPathName(s, name);

    fprintf(stderr, "[Atlas-pheap] Map existing file %s at base_address %p\n",
            s, base_addr);
    
    int fd = open(s, flags, flags == O_RDONLY ? S_IRUSR : S_IRUSR | S_IWUSR);
    assert(fd != -1);

#if !defined(NDEBUG)    
    int flock_status =
#endif        
        flock(fd, flags == O_RDONLY ? LOCK_SH : LOCK_EX);
    assert(!flock_status);

    void * mapped_addr = mmap(base_addr, FILESIZE, PROT_READ | PROT_WRITE,
                              MAP_SHARED, fd, 0);
    assert(mapped_addr != MAP_FAILED);

    return make_pair(mapped_addr, fd);
}

#ifdef PMM_OS
int NVM_CreateNewMappedFile(const char * name, int flags, void * base_addr)
{
    char s[MAXLEN];
    NVM_qualifyPathName(s, name);

    NVM_CreateLogDir();

    int fd = open(s, flags | O_CREAT | O_TRUNC,
                  flags == O_RDONLY  ? S_IRUSR : (S_IRUSR | S_IWUSR));
    assert(fd != -1);

    int flock_status = flock(fd, flags == O_RDONLY ? LOCK_SH : LOCK_EX);
    assert(!flock_status);

    off_t offt = lseek(fd, FILESIZE-1, SEEK_SET);
    assert(offt != -1);

    int result = write(fd, "", 1);
    assert(result != -1);
    base_addr=NULL;
    void * addr =
        mmap(base_addr, FILESIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(addr != MAP_FAILED);
    pmm_mmap_addr=addr;
    return fd;
}
#else
int NVM_CreateNewMappedFile(const char * name, int flags, void * base_addr)
{
    assert(base_addr);
    
    char s[MAXLEN];
    NVM_qualifyPathName(s, name);

    NVM_CreateLogDir();

    int fd = open(s, flags | O_CREAT | O_TRUNC,
                  flags == O_RDONLY  ? S_IRUSR : (S_IRUSR | S_IWUSR));
    assert(fd != -1);

#if !defined(NDEBUG)    
    int flock_status =
#endif        
        flock(fd, flags == O_RDONLY ? LOCK_SH : LOCK_EX);
    assert(!flock_status);

#if !defined(NDEBUG)    
    off_t offt =
#endif        
        lseek(fd, FILESIZE-1, SEEK_SET);
    assert(offt != -1);

#if !defined(NDEBUG)    
    int result =
#endif        
        write(fd, "", 1);
    assert(result != -1);

#if !defined(NDEBUG)    
    void * addr =
#endif        
        mmap(base_addr, FILESIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(addr != MAP_FAILED);
    assert(addr == base_addr);
    return fd;
}
#endif

// Incorporate access type in the Find/Create interfaces.
// The region table must always exist since this is something that
// should be maintained by the OS. Modify the following code to assume so.
// Additionally, create a separate binary that assumes that there is no
// region table and creates one. This should be used in conjunction with
// "make clean_memory" to simulate what an application should usually see.
// With the above change, NVM_SetupRegionTable does not need to flock the
// region table any more.

// While creating a new PR, flock the region_table file first, populate
// the necessary metadata, create/open the PR file, and call flock on the
// PR file with the appropriate flags. Then funlock the region_table file.
// In the FindRegion implementation, there is no need to flock the region
// table file in the current incarnation. But depending on the access mode,
// flock with appropriate flags must still be called on the PR file. The
// PR file should be funlocked with appropriate flags when close/delete
// is called on it.

// Create/open the persistent region corresponding to the region table
void NVM_SetupRegionTable(const char *suffix)
{
    int index;
    int status = pthread_mutexattr_settype(
        &alloc_mutex_attr, PTHREAD_MUTEX_RECURSIVE);
    assert(!status);

    for (index = 0; index < MAX_NUM_HEAPS; ++index)
    {
        status = pthread_mutex_init(&(AllocLockTab[index]), &alloc_mutex_attr);
        assert(!status);
    }

    bool does_region_table_exist;
    struct stat stat_buffer;
#if 0    
    char region_table_name[1024];
    sprintf(region_table_name, "/dev/shm/__nvm_system_region_table__%s",
            suffix);
#endif
#ifdef PMM_OS
    const char * name = "__nvm_system_region_table__";
    char region_table_name[MAXLEN];
    NVM_qualifyPathName(region_table_name,name);
#else
    const char *region_table_name = "/dev/shm/__nvm_system_region_table";
#endif

    if (!stat(region_table_name, &stat_buffer))
    {
        does_region_table_exist = true;
        region_table_fd = open(region_table_name, O_RDWR, S_IRUSR | S_IWUSR);
        assert(region_table_fd != -1);
    }
    else
    {
        does_region_table_exist = false;
        region_table_fd = open(region_table_name, O_RDWR | O_CREAT,
                               S_IRUSR | S_IWUSR);
        assert(region_table_fd != -1);

#if !defined(NDEBUG)        
        off_t offt =
#endif            
            lseek(region_table_fd, FILESIZE-1, SEEK_SET);
        assert(offt != -1);

#if !defined(NDEBUG)        
        int result =
#endif
            write(region_table_fd, "", 1);
        assert(result != -1);
    }

    region_table_addr =
        mmap((void *)0x300000000000, FILESIZE, PROT_READ | PROT_WRITE,
             MAP_SHARED, region_table_fd, 0);
    assert(region_table_addr != MAP_FAILED && "mmap failed");
    assert(region_table_addr == (void *)0x300000000000 && "requested base address not obtained --- this is not a tolerated failure");

    // if the table is getting created now, set the size to 0
    if (!does_region_table_exist)
    {
        intptr_t * tmp_ptr = (intptr_t *)region_table_addr;

        // No need to hold locks because threads haven't been spawned yet
        uint32_t * num_elem_ptr = (uint32_t *) nvm_alloc(
            sizeof(uint32_t), &tmp_ptr, (intptr_t *)region_table_addr, 0, 0);
        assert(num_elem_ptr);

        *num_elem_ptr = 0;

#if !defined(NDEBUG)        
        HeapEntry * start_table_ptr =
#endif            
            (HeapEntry *) nvm_alloc(
                MAX_NUM_HEAPS * sizeof(HeapEntry), &tmp_ptr,
            (intptr_t *)region_table_addr, 0, 0);
        assert(start_table_ptr);
#ifdef PMM_OS
        uint32_t num_entries = NVM_GetNumRegions();
        HeapEntry * region_entry = NVM_GetRegionMetaData(num_entries);
        assert(region_entry);
        strcpy(region_entry->name, region_table_name);
        region_entry->id = num_entries;
        region_entry->base_addr = (intptr_t *)region_table_addr;
        region_entry->curr_alloc_addr = region_entry->base_addr;
        // This is an example of an NV-to-Vptr. Use with caution
        region_entry->free_list = new FreeList;
        region_entry->file_desc = region_table_fd;
        region_entry->is_mapped = region_entry->is_valid = true;
        //printf("value of num-entries is %d\n",num_entries);
        NVM_SetNumRegions(num_entries + 1);
#endif

    }
}

uint32_t NVM_GetNumRegions()
{
    assert(region_table_addr);
    uint32_t n = *(uint32_t *)mem2ptr(region_table_addr);
    assert(n >= 0 && n < MAX_NUM_HEAPS);
    return n;
}

void NVM_SetNumRegions(uint32_t count)
{
    void * rgn_count_ptr = mem2ptr(region_table_addr);
    *(uint32_t *)rgn_count_ptr = count;
    NVM_FLUSH(rgn_count_ptr);
}


HeapEntry * NVM_GetRegionMetaData(uint32_t rid)
{
    char * md_mem = (char *)region_table_addr +
        NVM_get_actual_alloc_size(sizeof(uint32_t));
    char * md_ptr = (char *)(mem2ptr(md_mem)) + rid * sizeof(HeapEntry);
    assert(md_ptr);
    return (HeapEntry *)md_ptr;
}

// Process-level locks may not be held while executing the following.
// This should be ok. Even if another process is adding an entry to the
// region table, the num_entries field is written to and flushed out to
// memory only after the entry is populated. So the following routine
// should see either the previous or the new state of the region table, but
// not a half-populated state. If both the processes are interested in
// the same persistent region, then an flock does protect this routine, so
// that should work ok too.
#ifdef PMM_OS

pair<HeapEntry*,void*>  NVM_FindRegionMetaData(const char * name,bool ignore_attr)
{
    HeapEntry * md_ptr = NVM_GetTablePtr();
    uint32_t num_entries = NVM_GetNumRegions();

    uint32_t curr = 0;
    while (curr < num_entries)
    {
        if (md_ptr->is_valid && !strcmp(name, md_ptr->name)) return make_pair(md_ptr,(void*)0);
        ++ curr; ++ md_ptr;
    }
    return make_pair((HeapEntry*)0,(void*)0);
}
#else
pair<HeapEntry*,void*> NVM_FindRegionMetaData(
    const char *name, bool ignore_attr)
{
    // If all of the entries can be scanned, return the next valid address
    // for use as a base address of a region.
    char *base_addr = 0;
    HeapEntry *md_ptr = NVM_GetTablePtr();
    uint32_t num_entries = NVM_GetNumRegions();
    
    uint32_t curr = 0;
    while (curr < num_entries)
    {
        if ((ignore_attr || md_ptr->is_valid) && !strcmp(name, md_ptr->name))
            return make_pair(md_ptr,(void*)0);
        if ((char*)md_ptr->base_addr > base_addr)
            base_addr = (char*)md_ptr->base_addr;
        ++ curr; ++ md_ptr;
    }
    if (base_addr) base_addr += FILESIZE;
    return make_pair((HeapEntry*)0,(void*)base_addr);
}
#endif
void *NVM_ComputeNewBaseAddr()
{
    // Return the next valid address for use as a base address of a region
    char *base_addr = 0;
    HeapEntry *md_ptr = NVM_GetTablePtr();
    uint32_t num_entries = NVM_GetNumRegions();
    
    uint32_t curr = 0;
    while (curr < num_entries)
    {
        // ignoring valid bits for now
        if ((char*)md_ptr->base_addr > base_addr)
            base_addr = (char*)md_ptr->base_addr;
        ++ curr; ++ md_ptr;
    }
    // At the very least, the log must have been entered in the region table
    assert(base_addr);
    base_addr += FILESIZE;
    return (void*)base_addr;
}

HeapEntry * NVM_GetTablePtr()
{
    assert(region_table_addr);
    char * tptr = (char *)region_table_addr +
        NVM_get_actual_alloc_size(sizeof(uint32_t));
    tptr = (char *)(mem2ptr(tptr));
    return (HeapEntry *)tptr;
}

// TODO: We probably should have an ignore_valid_attr bit as well for
// calls during the recovery process.
// WARNING: This can only be called from the recovery process currently.
// To call otherwise, the interval map has to be properly updated.
pair<void*,uint32_t> NVM_EnsureMapped(
    intptr_t *addr, bool ignore_mapped_attr)
{
    HeapEntry *md_ptr = NVM_GetTablePtr();
    uint32_t num_entries = NVM_GetNumRegions();

    uint32_t curr = 0;
    while (curr < num_entries)
    {
        if (!md_ptr->is_valid)
        {
            ++ curr;
            ++ md_ptr;
            continue;
        }
        
        char *base_addr = (char*)md_ptr->base_addr;
        char *end_addr = base_addr + FILESIZE;

        if ((char*)addr >= base_addr && (char*)addr <= end_addr)
        {
            if (!md_ptr->is_mapped || ignore_mapped_attr)
            {
                pair<void*, int> res = NVM_MapExistingFile(
                    md_ptr->name, O_RDWR, md_ptr->base_addr);
                md_ptr->is_mapped = true;
                md_ptr->file_desc = res.second;
            }
            return make_pair(base_addr, md_ptr->id);
        }
        ++ curr; ++ md_ptr;
    }
    // error
    assert(0);
    return make_pair((void*)0, 0);
}

// We need a faster implementation for this, probably a tree-based
// implementation. Make sure that this interface is thread safe. When
// we have real NVRAM and the OS maps it to a unique address range,
// checking whether an address is persistent should be much cheaper.
pair<bool, uint32_t> NVM_GetRegionId(void * addr, size_t sz)
{
    HeapEntry * md_ptr = NVM_GetTablePtr();
    uint32_t num_entries = NVM_GetNumRegions();

    uint32_t curr = 0;
    while (curr < num_entries)
    {
        if (!md_ptr->is_valid || !md_ptr->is_mapped)
        {
            ++ curr;
            ++ md_ptr;
            continue;
        }
        
        char * base_addr = (char *)md_ptr->base_addr;
        char * end_addr = base_addr + FILESIZE;
        if ((char *)addr >= base_addr && (char *)addr < end_addr)
        {
#if !defined(NDEBUG)            
            char * last_addr = (char *)addr + sz;
            assert(last_addr >= base_addr && last_addr < end_addr);
#endif            
            return make_pair(true, md_ptr->id);
        }
        ++ curr; ++ md_ptr;
    }
    return make_pair(false, 0 /* dummy */);
}

uint32_t NVM_GetOpenRegionId(void *addr, size_t sz)
{
    MapInterval & intervals = *(MapInterval*)ALAR(map_interval_p);
    MapInterval::const_iterator ci =
        FindInMapInterval(intervals,
                          (uint64_t)addr,
                          (uint64_t)((char*)addr+sz-1));
    if (ci != intervals.end()) return ci->second;
    else return -1;
}

int NVM_IsInOpenPR(void *addr, size_t sz)
{
#if defined(_ALL_PERSISTENT)    
    return true;
#endif    
    if (!map_interval_p) return false;
    MapInterval & intervals = *(MapInterval*)ALAR(map_interval_p);
    if (FindInMapInterval(intervals,
                          (uint64_t)addr,
                          (uint64_t)((char*)addr+sz-1)) != intervals.end())
        return true;
    else return false;
}

void * NVM_GetRegionRoot(uint32_t id)
{
    HeapEntry *he = NVM_GetRegionMetaData(id);
    assert(he);
//    fprintf(stderr, "Region root is %p\n", mem2ptr(he->base_addr));
    return (void *)*(intptr_t*)(mem2ptr(he->base_addr));
}

void NVM_SetRegionRoot(uint32_t rid, void * ptr)
{
    HeapEntry * he = NVM_GetRegionMetaData(rid);
    // TODO: This should not be logged for the helper thread.
    NVM_STR2(*(intptr_t*)(mem2ptr(he->base_addr)), (intptr_t)ptr,
             sizeof(intptr_t)*8);
    
    // This must act like a release operation so that all prior
    // writes to NVRAM are flushed out. 
#if (defined(_FLUSH_LOCAL_COMMIT) || defined(_FLUSH_GLOBAL_COMMIT)) && \
    !defined(DISABLE_FLUSHES)
    if (tl_flush_ptr) 
    {
        FlushCacheLines(*tl_flush_ptr);
        tl_flush_ptr->clear();
    }
#endif
}

// Note: This function assumes that appropriate locks are already acquired
// if required. It may be called during single-threaded recovery.
void NVM_InsertToFreeList(HeapEntry * he, void * ptr)
{
    assert(he->free_list);
    FreeList & fl = *(he->free_list);
    
    size_t sz = NVM_get_requested_alloc_size_from_ptr(ptr);
    size_t bin_number;
    if (sz < MAX_FREE_CATEGORY)
    {
        // since alignment is 16 bytes and metadata size is 16 bytes,
        // you are going to get 32 bytes of actual allocation for requests
        // ranging from 1-16 bytes. So the buckets can be for 16, 32, 48,
        // 64, etc. 
        // How do you handle allocation of 0 bytes? Today, we are allocating
        // 16 bytes, a wastage. Should filter it out early. TODO.
        bin_number = (sz + NVM_ALIGN_MASK) & ~NVM_ALIGN_MASK;
    }
    else bin_number = MAX_FREE_CATEGORY;

    void * mem = ptr2mem(ptr);
    FreeList::iterator ci = fl.find(bin_number);
    if (ci == fl.end())
    {
        MemMap max_mem_map;
        max_mem_map.insert(make_pair(mem, true));
        fl.insert(make_pair(bin_number, max_mem_map));
    }
    else
    {
        ci->second.insert(make_pair(mem, true));
    }
}

// This can be expensive. Optimizations are possible. TODO.
// (1) Demand-driven approach
// (2) Maintain the free list within persistent RAM
// (3) Main multiple free lists, a small one in persistent RAM and others
// in DRAM

// Note: No locks are acquired here. So this must be called from a
// single-threaded setting.
void BuildFreeList(HeapEntry * rgn_entry)
{
    char * mem = (char *)(rgn_entry->base_addr);

    // Do not assert the following. The free list pointer will be 0
    // if we had a clean shutdown. For a crash, the pointer may be non-zero
    // but bogus.
    // assert(!rgn_entry->free_list);
    rgn_entry->free_list = new FreeList;
    
    while (mem < (char *)(rgn_entry->curr_alloc_addr))
    {
        if (!NVM_Is_Mem_Allocated(mem))
            NVM_InsertToFreeList(rgn_entry, mem2ptr(mem));
        mem += NVM_get_actual_alloc_size(
            NVM_get_requested_alloc_size_from_mem(mem));
    }
}

// We scan the region for a free slot until we find one that can satisfy
// the alloc request. We add to the free list only the ones that were
// scanned. There is some redundancy in the scan that can be optimized. Note
// that this routine can be quite expensive.

// Note: All appropriate locks must be held at this point, if required.
pair<void*,bool> BuildPartialFreeList(size_t sz, HeapEntry *rgn_entry)
{
    assert(rgn_entry);
    char *mem = (char*)(rgn_entry->base_addr);

    while (mem < (char*)(rgn_entry->curr_alloc_addr))
    {
        if (!NVM_Is_Mem_Allocated(mem))
        {
            if (sz < NVM_get_requested_alloc_size_from_mem(mem)+1)
            {
                void *ret = (void*)(mem + NVM_get_metadata_size());
                *((size_t*)mem) = sz;
                *((size_t*)(mem + sizeof(size_t))) = true;
                assert(NVM_Is_Mem_Allocated(mem));
                return make_pair(ret, true);
            }
            else NVM_InsertToFreeList(rgn_entry, mem2ptr(mem));
        }
        mem += NVM_get_actual_alloc_size(
            NVM_get_requested_alloc_size_from_mem(mem));
    }
    return make_pair((void*)NULL, false);
}

void RegionDump(HeapEntry * he)
{
#ifdef ALLOC_DUMP
    if (getenv("REGION_DUMP"))
    {
        fprintf(stderr, "---------- Region Info ----------\n");
        
        fprintf(stderr, "name = %s id = %d\n", he->name, he->id);
        fprintf(stderr, "base_addr = %p end_addr = %p curr_addr = %p free_list = %p\n",
                he->base_addr, (char *)(he->base_addr)+FILESIZE,
                he->curr_alloc_addr, he->free_list);
        fprintf(stderr, "file = %d is_mapped = %s is_valid = %s\n",
                he->file_desc, he->is_mapped ? "yes" : "no",
                he->is_valid ? "yes" : "no");

        fprintf(stderr, "----------------------------------\n");
    }
#endif
}

void RegionTableDump()
{
#ifdef ALLOC_DUMP
    uint32_t num_entries = NVM_GetNumRegions();
    if (getenv("REGION_DUMP"))
        fprintf(stderr, "Num regions = %d\n", num_entries);
        
    HeapEntry * md_ptr = NVM_GetTablePtr();
    uint32_t curr = 0;
    while (curr < num_entries)
    {
        RegionDump(md_ptr);
        ++ curr; ++ md_ptr;
    }
#endif
}
