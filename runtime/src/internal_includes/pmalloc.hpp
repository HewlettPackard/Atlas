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
 

#ifndef PMALLOC_HPP
#define PMALLOC_HPP

#include <iostream>
#include <cstdlib>
#include <cassert>
#include <map>

#include <pthread.h>

#include "pregion_configs.hpp"

#include "atlas_api.h"

namespace Atlas {
    
typedef std::map<void* /* chunk address */, bool /* dummy */> MemMap;
typedef std::map<uint32_t /* bin number */, MemMap> FreeList;

// Physically persistent arena, contains logically transient data as well
class PArena {
public:
    explicit PArena() : CurrAllocAddr_{nullptr}, StartAddr_{nullptr},
        EndAddr_{nullptr}, ActualAlloced_{0}, FreeList_{new FreeList}
        { pthread_mutex_init(&Lock_, NULL); flushDirtyCacheLines(); }
    
    ~PArena()
        { if (FreeList_) { delete FreeList_; FreeList_ = nullptr; } }

    PArena(const PArena&) = delete;
    PArena(PArena&&) = delete;
    PArena& operator=(const PArena&) = delete;
    PArena& operator=(PArena&&) = delete;
    
    void initAllocAddresses(void *start_addr);
    void initTransients();
    
    void *get_curr_alloc_addr() const { return CurrAllocAddr_; }
    void *get_start_addr() const { return StartAddr_; }
    void *get_end_addr() const { return EndAddr_; }
    uint64_t get_actual_alloced() const { return ActualAlloced_; }
    
    bool doesRangeCheck(void *start, size_t sz) const
        { return start >= StartAddr_ &&
                (static_cast<char*>(start) + sz) <
                static_cast<char*>(EndAddr_); }

    void *allocMem(
        size_t sz, bool does_need_cache_line_alignment,
        bool does_need_logging);
    void *allocFromFreeList(
        size_t sz, bool does_need_cache_line_alignment,
        bool does_need_logging);
    void *allocFromUpdatedFreeList(
        size_t sz, bool does_need_cache_line_alignment,
        bool does_need_logging);
    void *allocRawMem(size_t);

    void freeMem(void *ptr, bool should_log);

    void Lock() { pthread_mutex_lock(&Lock_); }
    int tryLock() { return pthread_mutex_trylock(&Lock_); }
    void Unlock() { pthread_mutex_unlock(&Lock_); }
    
private:
    // The 3 addresses below are persistent. Their updates are flushed
    // but not logged.
    void *CurrAllocAddr_; // bump pointer
    void *StartAddr_;     
    void *EndAddr_;

    // The following is persistent as well. Its update is flushed
    // but not logged. For accuracy purposes, start with a fresh
    // region when the stats are turned on --- otherwise, allocation
    // from a prior run may not be captured. 
    // This field is unconditionally included here to avoid
    // layout incompatibility but updated only under the stats flag.
    uint64_t ActualAlloced_;
    
    // The following are considered logically transient and hence not
    // flushed. They must be reset at init time.
    pthread_mutex_t Lock_;
    FreeList *FreeList_;

    void flushDirtyCacheLines()
        { NVM_FLUSH(&CurrAllocAddr_); NVM_FLUSH(&ActualAlloced_); }
    
    void *carveExtraMem(char *mem, size_t actual_sz, size_t actual_free_sz);
            
    void insertToFreeList(uint32_t bin_no, void *mem);
    void deleteFromFreeList(uint32_t bin_no, void *mem);

    void incrementActualAllocedStats(size_t sz);
    void decrementActualAllocedStats(size_t sz);
};

inline void PArena::initAllocAddresses(void *start_addr)
{
    StartAddr_ = CurrAllocAddr_ = start_addr;
    EndAddr_ = static_cast<void*>(
        static_cast<char*>(start_addr) + kArenaSize_);
    flushDirtyCacheLines();
}

inline void PArena::initTransients()
{
    pthread_mutex_init(&Lock_, NULL);
    FreeList_ = new FreeList; 
}

inline void PArena::incrementActualAllocedStats(size_t sz)
{
#if defined(ATLAS_ALLOC_STATS)
    ActualAlloced_ += sz;
    NVM_FLUSH(&ActualAlloced_);
#endif
}
    
inline void PArena::decrementActualAllocedStats(size_t sz)
{
#if defined(ATLAS_ALLOC_STATS)
    ActualAlloced_ -= sz;
    NVM_FLUSH(&ActualAlloced_);
#endif
}
            
} // namespace Atlas

#endif
