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
 

#include <cassert>

#include "pregion.hpp"

namespace Atlas {

///
/// Entry point for region-based allocation
///    
void *PRegion::allocMem(
    size_t sz, bool does_need_cache_line_alignment, bool does_need_logging)
{
    assert(!IsDeleted_ && "Attempt to allocate memory from deleted region!");
    assert(IsMapped_ && "Attempt to allocate memory from unmapped region!");

    // adjust current arena if required
    if (!PMallocUtil::is_valid_tl_curr_arena(Id_))
        PMallocUtil::set_tl_curr_arena(
            Id_, (uint64_t)pthread_self() % kNumArenas_);

    void *alloc_ptr = nullptr;
    bool should_update_free_list = false;
    if ((alloc_ptr = allocMemFromArenas(
             sz, should_update_free_list,
             does_need_cache_line_alignment, does_need_logging))) {
        assert(doesRangeCheck(alloc_ptr,
                              PMallocUtil::get_actual_alloc_size(sz)) &&
               "Attempt to allocate memory outside the requested region!");
        return alloc_ptr;
    }

    // Now the expensive part since free lists need updating: This is done
    // only when all arenas appear full, including the existing free lists.
    should_update_free_list = true;
    if ((alloc_ptr = allocMemFromArenas(
             sz, should_update_free_list,
             does_need_cache_line_alignment, does_need_logging))) {
        assert(doesRangeCheck(alloc_ptr,
                              PMallocUtil::get_actual_alloc_size(sz)) &&
               "Attempt to allocate memory outside the requested region!");
        return alloc_ptr;
    }

    // TODO msgs
    fprintf(stderr, "[Atlas-pheap] Allocation failed in region %d\n", Id_);
#ifdef _ATLAS_ALLOC_STATS    
    fprintf(stderr, "[Atlas-pheap] Requested = %ld Alloced = %ld\n",
            pheap->requested_sz, pheap->alloced_sz);
#endif    
    assert(alloc_ptr && "Out of memory in this persistent region!");
    return nullptr;
}

///
/// Traverse the arenas, try to allocate available memory from an
/// unlocked one, otherwise use the free list
///    
void *PRegion::allocMemFromArenas(
    size_t sz, bool should_update_free_list,
    bool does_need_cache_line_alignment, bool does_need_logging)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    assert(PMallocUtil::get_tl_curr_arena(Id_) < kNumArenas_ &&
           "Arena index is out of range!");
    
    bool arena_tracker[kNumArenas_];
    memset(arena_tracker, 0, sizeof(arena_tracker));
    
    // Start with the arena used the last time, reducing false sharing
    uint32_t arena_count = 0;
    do  {
        // look inside an arena only once
        if (arena_tracker[PMallocUtil::get_tl_curr_arena(Id_)]) {
            PMallocUtil::set_tl_curr_arena(
                Id_, PMallocUtil::get_tl_next_arena(Id_));
            continue;
        }
        
        PArena *parena = getArena(PMallocUtil::get_tl_curr_arena(Id_));
        int status;
        // loop until an arena can be locked by this thread
        while ((status = parena->tryLock())) {
            assert(status == EBUSY && "Trylock returned unexpected status!");
            PMallocUtil::set_tl_curr_arena(
                Id_, PMallocUtil::get_tl_next_arena(Id_));
            parena = getArena(PMallocUtil::get_tl_curr_arena(Id_));
        }
        arena_tracker[PMallocUtil::get_tl_curr_arena(Id_)] = true;

        void *alloc_ptr = nullptr;
        if (!should_update_free_list) {
            if ((alloc_ptr = parena->allocMem(
                     sz, does_need_cache_line_alignment, does_need_logging))) {
                parena->Unlock();
                return alloc_ptr;
            }
            if ((alloc_ptr = parena->allocFromFreeList(
                     sz, does_need_cache_line_alignment, does_need_logging))) {
                parena->Unlock();
                return alloc_ptr;
            }
        }
        else if ((alloc_ptr = parena->allocFromUpdatedFreeList(
                      sz, does_need_cache_line_alignment,
                      does_need_logging))) {
            parena->Unlock();
            return alloc_ptr;
        }

        parena->Unlock();
        
        ++arena_count;

        // round robin if the current arena is full
        PMallocUtil::set_tl_curr_arena(
            Id_, PMallocUtil::get_tl_next_arena(Id_));
    }while (arena_count < kNumArenas_);

    return nullptr;
}

///
/// Entry point for region-based calloc
///    
void *PRegion::callocMem(size_t nmemb, size_t sz)
{
    bool does_need_cache_line_alignment = false;
    bool does_need_logging = true;
    void *calloc_mem = allocMem(nmemb * sz, does_need_cache_line_alignment,
                                does_need_logging);
    memset(calloc_mem, 0, nmemb * sz);
    return calloc_mem;
}

///
/// Entry point for region-based realloc
///    
void *PRegion::reallocMem(void *ptr, size_t sz)
{
    bool does_need_cache_line_alignment = false;
    bool does_need_logging = true;
    if (!ptr && sz) return allocMem(sz, does_need_cache_line_alignment,
                                    does_need_logging);
    if (!sz) {
        freeMem(ptr, does_need_logging);
        return nullptr;
    }
    size_t curr_sz = PMallocUtil::get_requested_alloc_size_from_ptr(ptr);
    void *realloced_ptr = allocMem(sz, does_need_cache_line_alignment,
                                   does_need_logging);
    memcpy(realloced_ptr, ptr, curr_sz < sz ? curr_sz : sz);
    freeMem(ptr, does_need_logging);
    return realloced_ptr;
}

///
/// Flush out persistent metadata of a persistent region
///    
void PRegion::flushDirtyCacheLines()
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    NVM_FLUSH(&Id_);
    if (PMallocUtil::is_on_different_cache_line(&Id_, &IsDeleted_))
        NVM_FLUSH(&IsDeleted_);
    assert(kMaxlen_);
    NVM_FLUSH(&Name_[0]);
    if (PMallocUtil::is_on_different_cache_line(
            &Name_[0], &Name_[kMaxlen_ - 1]))
        NVM_FLUSH(&Name_[kMaxlen_ - 1]);
    // arenas flushed out separately
}

} // namespace Atlas
