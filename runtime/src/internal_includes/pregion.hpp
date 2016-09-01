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
 

#ifndef PREGION_HPP
#define PREGION_HPP

#include <cassert>
#include <cstring>

#include "atlas_api.h"
#include "internal_api.h"

#include "pmalloc.hpp"
#include "pmalloc_util.hpp"

namespace Atlas {

class PRegion {
public:
    explicit PRegion(const char *nm, region_id_t rid, void *ba) 
        : Id_{rid}, BaseAddr_{ba}, IsMapped_{true}, IsDeleted_{false},
        FileDesc_{-1} {
            assert(strlen(nm) < kMaxlen_+1);
            std::strcpy(Name_, nm);
            initArenaAllocAddresses();
            PMallocUtil::set_default_tl_curr_arena(rid);
            flushDirtyCacheLines();
        }
    ~PRegion() = default;
    PRegion(const PRegion&) = delete;
    PRegion(PRegion&&) = delete;
    PRegion& operator=(const PRegion&) = delete;
    PRegion& operator=(PRegion&&) = delete;
                 
    void set_is_mapped(bool im) { IsMapped_ = im; NVM_FLUSH(&IsMapped_); }
    void set_is_deleted(bool id) { IsDeleted_ = id; NVM_FLUSH(&IsDeleted_); }
    void set_file_desc(int fd) { FileDesc_ = fd; }

    region_id_t get_id() const { return Id_; }
    void *get_base_addr() const { return BaseAddr_; }
    bool is_mapped() const { return IsMapped_; }
    bool is_deleted() const { return IsDeleted_; }
    int get_file_desc() const { return FileDesc_; }
    const char *get_name() const { return Name_; }

    bool doesRangeCheck(const void *ptr, size_t sz) const
        { return ptr >= BaseAddr_ &&
                (static_cast<const char*>(ptr) + sz) <
                (static_cast<char*>(BaseAddr_) + kPRegionSize_); }
    
    PArena *getArena(uint32_t index)
        { assert(index < kNumArenas_); return &Arena_[index]; }
            
    void *allocMem(
        size_t sz, bool does_need_cache_line_alignment, 
        bool does_need_logging);
    void *callocMem(size_t nmemb, size_t sz);
    void *reallocMem(void*, size_t);
    void  freeMem(void *ptr, bool should_log);

    void setRoot(void *new_root)
        {     
            // TODO: This should not be logged for the helper
            // thread. This looks like a bogus comment. The helper
            // thread does not call setRoot. Check back later.

            // More problems: Though the following may be ok, it
            // is better to mfence after the following flush. NVM_STR2
            // may not call flush (for table-based scheme) until later
            // and does not call mfence until later.
            NVM_STR2(*(static_cast<intptr_t*>(
                           PMallocUtil::mem2ptr(BaseAddr_))),
                     reinterpret_cast<intptr_t>(new_root),
                     sizeof(intptr_t)*8);
            full_fence();
        }
            
    void *getRoot() const
        { return reinterpret_cast<void*>(
              *(static_cast<intptr_t*>(
                    PMallocUtil::mem2ptr(BaseAddr_))));
        }
            
    void *allocRoot()
        {
            // Must be at a known offset, so bypass regular allocation
            assert(kNumArenas_);
            return Arena_[0].allocRawMem(sizeof(intptr_t));
        }

    void  initArenaTransients()
        {
            for (uint32_t i = 0; i < kNumArenas_; ++i)
                getArena(i)->initTransients();
        }

    void dumpDebugInfo() const;
    void printStats();
    
private:
    // Region metadata follows. Except for the file descriptor, all of
    // them are persistent and must be properly flushed out.

    // If any change to the following layout is made, the flushing
    // code may need to change as well.
    region_id_t Id_; 
    void *BaseAddr_;
    bool IsMapped_;
    bool IsDeleted_;
    int FileDesc_;
    char Name_[kMaxlen_];
    PArena Arena_[kNumArenas_];

    void initArenaAllocAddresses();
    void *allocMemFromArenas(
        size_t sz, bool should_update_free_list,
        bool does_need_cache_line_alignment, bool does_need_logging);
    void flushDirtyCacheLines();
};

inline void PRegion::freeMem(void *ptr, bool should_log)
{
    uint32_t arena_index = (reinterpret_cast<intptr_t>(ptr) -
                            reinterpret_cast<intptr_t>(BaseAddr_))/kArenaSize_;
    getArena(arena_index)->freeMem(ptr, should_log);
}

inline void PRegion::initArenaAllocAddresses()
{
    for (uint32_t i = 0; i < kNumArenas_; ++i)
        getArena(i)->initAllocAddresses(
            static_cast<char*>(BaseAddr_) + i * kArenaSize_);
}

///
/// Used with the debug flag below
///    
inline void PRegion::dumpDebugInfo() const
{
#if defined(ATLAS_ALLOC_DUMP)    
    std::cout << Name_ << " " << Id_ << " " << BaseAddr_ << " " <<
        (IsMapped_ ? "mapped " : "unmapped ") <<
        (IsDeleted_ ? "deleted " : "valid ") << std::endl;
#endif    
}

///
/// Used with the stats flag below
///
inline void PRegion::printStats() 
{
#if defined(ATLAS_ALLOC_STATS)
    uint64_t total_alloced = 0;
    for (uint32_t i = 0; i < kNumArenas_; ++i)
        total_alloced += getArena(i)->get_actual_alloced();
    std::cout << "[Atlas] Total bytes allocated in region " <<
        Name_ << ":" << total_alloced << std::endl;
#endif
}
        
} // namespace Atlas

#endif

