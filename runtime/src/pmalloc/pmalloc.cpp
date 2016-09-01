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
 

#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <utility>

#include "pmalloc.hpp"
#include "pmalloc_util.hpp"
#include "internal_api.h"
#include "atlas_alloc.h"

namespace Atlas {

uint32_t PMallocUtil::CacheLineSize_{UINT32_MAX};
uintptr_t PMallocUtil::CacheLineMask_{UINTPTR_MAX};
thread_local uint32_t PMallocUtil::TL_CurrArena_[kMaxNumPRegions_] = {};

///
/// Given a pointer to persistent memory, mark the location free and
/// add it to the free list. 
///    
void PArena::freeMem(void *ptr, bool should_log)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    Lock();

    if (!PMallocUtil::is_ptr_allocated(ptr))
    {
        fprintf(stderr, "[Atlas-pheap] assert: %p %ld %ld\n",
                ptr, *((size_t *)ptr),
                *(size_t *)((char *)ptr+sizeof(size_t)));
        assert(PMallocUtil::is_ptr_allocated(ptr) &&
               "free called on unallocated memory");
    }

    char *mem = (char*)PMallocUtil::ptr2mem(ptr);
    assert(doesRangeCheck(mem, *(reinterpret_cast<size_t*>(mem))) &&
           "Attempt to free memory outside of arena range!");
    
#ifndef _DISABLE_ALLOC_LOGGING
    if (should_log) nvm_log_free(mem + sizeof(size_t));
#endif
    
    *(size_t*)(mem + sizeof(size_t)) = false;
    NVM_FLUSH(mem + sizeof(size_t));
    
    insertToFreeList(PMallocUtil::get_bin_number(
                         PMallocUtil::get_requested_alloc_size_from_ptr(ptr)), 
                     PMallocUtil::ptr2mem(ptr));

    decrementActualAllocedStats(
        PMallocUtil::get_actual_alloc_size(
            PMallocUtil::get_requested_alloc_size_from_ptr(ptr)));
    
    Unlock();
}

///
/// Given a size, allocate memory using the bump pointer. If it
/// reaches the end of the arena, return null.
///    
void *PArena::allocMem(
    size_t sz, bool does_need_cache_line_alignment, bool does_need_logging)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    // lock already acquired
    size_t alloc_sz = PMallocUtil::get_actual_alloc_size(sz);
    char *curr_alloc_addr_c = static_cast<char*>(CurrAllocAddr_);
    intptr_t curr_alloc_addr_i = reinterpret_cast<intptr_t>(CurrAllocAddr_);
    if (does_need_cache_line_alignment)
    {
        intptr_t cache_line = curr_alloc_addr_i &
            PMallocUtil::get_cache_line_mask();
        intptr_t next_cache_line = cache_line +
            PMallocUtil::get_cache_line_size();
        if (reinterpret_cast<intptr_t>(
                curr_alloc_addr_c + PMallocUtil::get_metadata_size()) !=
            next_cache_line)
        {
            if (reinterpret_cast<char*>(next_cache_line) - 
                PMallocUtil::get_metadata_size() + alloc_sz >
                static_cast<char*>(EndAddr_))
                return nullptr;
            
            intptr_t diff = next_cache_line - curr_alloc_addr_i -
                PMallocUtil::get_metadata_size();
            assert(diff >= static_cast<intptr_t>(
                       PMallocUtil::get_smallest_actual_alloc_size()) &&
                   "Insufficient space for metadata!");

            *(static_cast<size_t*>(CurrAllocAddr_)) =
                diff - PMallocUtil::get_metadata_size();

            // No need to log the following since it is not user visible

            // Mark it free
            *(reinterpret_cast<size_t*>(
                  curr_alloc_addr_c + sizeof(size_t))) = false; 

            // The above metadata updates are to the same cache line
            assert(!isOnDifferentCacheLine(
                       curr_alloc_addr_c, curr_alloc_addr_c + sizeof(size_t)));

            NVM_FLUSH(curr_alloc_addr_c);

            insertToFreeList(PMallocUtil::get_bin_number(
                                 diff - PMallocUtil::get_metadata_size()), 
                             curr_alloc_addr_c);
            CurrAllocAddr_ = reinterpret_cast<void*>(
                next_cache_line - PMallocUtil::get_metadata_size());
            NVM_FLUSH(&CurrAllocAddr_);
            curr_alloc_addr_c = static_cast<char*>(CurrAllocAddr_);
        }
    }
    if ((curr_alloc_addr_c + alloc_sz - 1) < static_cast<char*>(EndAddr_))
    {
        *(static_cast<size_t*>(CurrAllocAddr_)) = sz;

#ifndef _DISABLE_ALLOC_LOGGING
        if (does_need_logging) nvm_log_alloc(
            curr_alloc_addr_c + sizeof(size_t));
#endif
        
        *(reinterpret_cast<size_t*>(
              curr_alloc_addr_c + sizeof(size_t))) = true;

        // The above metadata updates are to the same cache line
        assert(!isOnDifferentCacheLine(
                   curr_alloc_addr_c, curr_alloc_addr_c + sizeof(size_t)));

        NVM_FLUSH(curr_alloc_addr_c);

        // If we fail somewhere above, the above memory will be
        // considered unallocated because CurrAllocAddr_ is not yet set.
        CurrAllocAddr_ = static_cast<void*>(curr_alloc_addr_c + alloc_sz);
        NVM_FLUSH(&CurrAllocAddr_);

        // If we fail here or later, the above memory may be leaked.

        if (does_need_cache_line_alignment)
            assert(PMallocUtil::is_cache_line_aligned(
                       curr_alloc_addr_c + PMallocUtil::get_metadata_size()));

        incrementActualAllocedStats(alloc_sz);
        
        return static_cast<void*>(
            curr_alloc_addr_c + PMallocUtil::get_metadata_size());
    }
    return nullptr;
}

///
/// Given a size, allocate memory from the arena free list, if
/// possible.
///    
void *PArena::allocFromFreeList(
    size_t sz, bool does_need_cache_line_alignment, bool does_need_logging)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    // TODO: add support for cache line alignment
    if (does_need_cache_line_alignment) return nullptr;
    
    if (FreeList_->empty()) return nullptr;

    size_t actual_sz = PMallocUtil::get_actual_alloc_size(sz);
    uint32_t bin_number = PMallocUtil::get_bin_number(sz);
    while (bin_number < kMaxFreeCategory_ + 1)
    {
        FreeList::iterator ci = FreeList_->find(bin_number);
        // Look in the existing bin. We don't look for additional memory
        // that may have been freed. We will do that later.
        if (ci != FreeList_->end())
        {
            MemMap & mem_map = ci->second;
            MemMap::iterator mem_ci_end = mem_map.end();
            for (MemMap::iterator mem_ci = mem_map.begin();
                 mem_ci != mem_ci_end; ++mem_ci)
            {
                char *mem = static_cast<char*>(mem_ci->first);
                assert(!PMallocUtil::is_mem_allocated(mem) &&
                       "Location in free list is marked allocated!");

                void *carved_mem = nullptr;
                if (bin_number == kMaxFreeCategory_)
                {
                    // carve out the extra memory if possible
                    size_t actual_free_sz = PMallocUtil::get_actual_alloc_size(
                        *(reinterpret_cast<size_t*>(mem)));

                    if (actual_sz > actual_free_sz) // cannot satisfy
                        continue;
                    else if (actual_sz +
                             PMallocUtil::get_smallest_actual_alloc_size() <=
                             actual_free_sz)
                        carved_mem = carveExtraMem(
                            mem, actual_sz, actual_free_sz);
                    else assert(actual_sz == actual_free_sz);
                }
                // If we fail here, the above carving does not take effect

                *(reinterpret_cast<size_t*>(mem)) = sz;

                // If we fail here or anywhere above, no memory is leaked

#ifndef _DISABLE_ALLOC_LOGGING
                if (does_need_logging) nvm_log_alloc(mem + sizeof(size_t));
#endif
                
                *(reinterpret_cast<size_t*>(mem + sizeof(size_t))) = true;

                // The above metadata updates are to the same cache line
                assert(!isOnDifferentCacheLine(
                           mem, mem + sizeof(size_t)));

                NVM_FLUSH(mem);
                
                // If we fail here, the above allocated memory may be leaked

                mem_map.erase(mem_ci);

                if (carved_mem) insertToFreeList(
                    PMallocUtil::get_bin_number(
                        *(static_cast<size_t*>(carved_mem))),
                    carved_mem);

                incrementActualAllocedStats(actual_sz);
                
                return static_cast<void*>(mem +
                                          PMallocUtil::get_metadata_size());
            }
        }
        bin_number = PMallocUtil::get_next_bin_number(bin_number);
    }
    return nullptr;
}

///
/// Given a size, traverse the arena from the start while looking for
/// a free chunk of memory that can satisfy the allocation
/// request. The arena free list is updated as the traversal is
/// performed.
///    
void *PArena::allocFromUpdatedFreeList(
    size_t sz, bool does_need_cache_line_alignment, bool does_need_logging)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    // TODO: add support for cache line alignment
    if (does_need_cache_line_alignment) return nullptr;

    size_t actual_sz = PMallocUtil::get_actual_alloc_size(sz);
    char *mem = static_cast<char*>(StartAddr_);
    
    while (mem < (char*)CurrAllocAddr_)
    {
        size_t mem_sz = PMallocUtil::get_requested_alloc_size_from_mem(mem);
        size_t actual_mem_sz = PMallocUtil::get_actual_alloc_size(mem_sz);
        if (!PMallocUtil::is_mem_allocated(mem))
        {
            if (actual_sz > actual_mem_sz)
            {
                // This address may be in the free list already. But the
                // implementation ensures no duplicates are added.
                insertToFreeList(
                    PMallocUtil::get_bin_number(
                        *(reinterpret_cast<size_t*>(mem))),
                    mem);
                mem += actual_mem_sz;
                continue;
            }

            void *carved_mem = nullptr;
            if (actual_sz + PMallocUtil::get_smallest_actual_alloc_size() <=
                actual_mem_sz)
                carved_mem = carveExtraMem(mem, actual_sz, actual_mem_sz);
            else assert(actual_sz == actual_mem_sz);

            // If we fail here, the above carving does not take effect
            
            *(reinterpret_cast<size_t*>(mem)) = sz;

            // If we fail here or anywhere above, no memory is leaked

#ifndef _DISABLE_ALLOC_LOGGING
            if (does_need_logging) nvm_log_alloc(mem + sizeof(size_t));
#endif
            
            *(reinterpret_cast<size_t*>(mem + sizeof(size_t))) = true;

            // The above metadata updates are to the same cache line
            assert(!isOnDifferentCacheLine(
                       mem, mem + sizeof(size_t)));

            NVM_FLUSH(mem);

            // If we fail here, the above allocated memory may be leaked
            deleteFromFreeList(PMallocUtil::get_bin_number(mem_sz), mem);
            
            if (carved_mem)
                insertToFreeList(
                    PMallocUtil::get_bin_number(
                        *(reinterpret_cast<size_t*>(carved_mem))),
                    carved_mem);

            incrementActualAllocedStats(actual_sz);
            
            return static_cast<void*>(mem + PMallocUtil::get_metadata_size());
        }
        mem += actual_mem_sz;
    }
    return nullptr;
}

///
/// Special form of "raw" memory allocation using the bump pointer.
///    
void *PArena::allocRawMem(size_t sz)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    size_t alloc_sz = PMallocUtil::get_actual_alloc_size(sz);
    assert((static_cast<char*>(CurrAllocAddr_)+alloc_sz-1) <
           static_cast<char*>(EndAddr_) && "Out of arena memory!");
    
    void *ret = static_cast<void*>(static_cast<char*>(CurrAllocAddr_) +
                                   PMallocUtil::get_metadata_size());

    *(static_cast<size_t*>(CurrAllocAddr_)) = sz;

    // Note that raw allocation is not logged

    *(reinterpret_cast<size_t*>(
          static_cast<char*>(CurrAllocAddr_) + sizeof(size_t))) = true;

    // The above metadata updates are to the same cache line
    assert(!isOnDifferentCacheLine(
               CurrAllocAddr_,
               static_cast<char*>(CurrAllocAddr_) + sizeof(size_t)));

    NVM_FLUSH(CurrAllocAddr_);
    
    // If a crash happens here, the memory appears allocated but it is not
    // assigned to any program-visible entity, hence it is essentially lost.
    
    CurrAllocAddr_ = static_cast<void*>(
        static_cast<char*>(CurrAllocAddr_) + alloc_sz);
    NVM_FLUSH(&CurrAllocAddr_);

    incrementActualAllocedStats(alloc_sz);
    
    return ret;
}

///
/// Add the specified chunk to a particular bin of the arena freelist
///    
void PArena::insertToFreeList(uint32_t bin_no, void *mem)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    FreeList::iterator ci = FreeList_->find(bin_no);
    if (ci == FreeList_->end()) {
        MemMap mem_map;
        mem_map.insert(std::make_pair(mem, true));
        FreeList_->insert(std::make_pair(bin_no, mem_map));
    }
    else ci->second.insert(std::make_pair(mem, true));
}

///
/// Delete the specified chunk from a particular bin of the freelist
///    
void PArena::deleteFromFreeList(uint32_t bin_no, void *mem)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    FreeList::iterator ci = FreeList_->find(bin_no);
    if (ci == FreeList_->end()) return; // tolerate absence of entry
    else 
    {
        MemMap & mem_map = ci->second;
        MemMap::iterator mem_ci = mem_map.find(mem);
        if (mem_ci == mem_map.end()) return;
        else mem_map.erase(mem_ci);
    }
}

///
/// Given a free chunk, an allocatable size, and the original free
/// size, carve it into two such that the second one is the new free
/// chunk after considering the first one as the allocated chunk.
///    
void *PArena::carveExtraMem(
    char *mem, size_t actual_alloc_sz, size_t actual_free_sz)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    void *carved_mem = static_cast<void*>(mem + actual_alloc_sz);
    *(static_cast<size_t*>(carved_mem)) =
        actual_free_sz - actual_alloc_sz - PMallocUtil::get_metadata_size();

    // No need to log the following since it is not user visible
    *(reinterpret_cast<size_t*>(
          static_cast<char*>(carved_mem) + sizeof(size_t))) = false;
    
    // The above metadata updates are to the same cache line
    assert(!isOnDifferentCacheLine(
               carved_mem, static_cast<char*>(carved_mem) + sizeof(size_t)));

    NVM_FLUSH(carved_mem);
    
    return carved_mem;
}

} // namespace Atlas
