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
 

#ifndef PREGION_MGR_HPP
#define PREGION_MGR_HPP

#include <cstdlib>
#include <cassert>
#include <utility>
#include <atomic>

#include <stdint.h>
#include <pthread.h>
#include <sys/file.h>

#include "pregion_configs.hpp"
#include "pregion_mgr_util.hpp"
#include "pregion.hpp"

namespace Atlas {

class PRegionMgr {
    static PRegionMgr *Instance_;
public:
    // serial mode only
    static PRegionMgr& createInstance() {
        assert(!Instance_);
        Instance_ = new PRegionMgr();
        Instance_->initPRegionTable();
        Instance_->setCacheParams();
        return *Instance_;
    }

    // serial mode only
    static void deleteInstance() {
        if (Instance_) {
            Instance_->shutPRegionTable();
            delete Instance_;
            Instance_ = nullptr;
        }
    }
            
    static PRegionMgr& getInstance() {
        assert(Instance_);
        return *Instance_;
    }

    static bool hasInstance() { return Instance_ != nullptr; }

    region_id_t getOpenPRegionId(const void *addr, size_t sz) const;

    bool isInOpenPRegion(const void *addr, size_t sz) const
        { return getOpenPRegionId(addr, sz) != kInvalidPRegion_; }

    PRegion *getPRegion(region_id_t rid) const {
        assert(rid < getNumPRegions() && "Region index out of range!");
        return instantiateNewPRegion(rid);
    }

    void *allocMem(
        size_t sz, region_id_t rid,
        bool does_need_cache_line_alignment, bool does_need_logging) const;
    void *callocMem(size_t nmemb, size_t sz, region_id_t) const;
    void *reallocMem(void*, size_t, region_id_t) const;
    void  freeMem(void *ptr, bool should_log = true) const;
    void  deleteMem(void *ptr, bool should_log = true) const;
    void freeMemImpl(region_id_t rgn_id, void *ptr, bool should_log) const;
    
    void *allocMemWithoutLogging(size_t sz, region_id_t rid) const;
    void *allocMemCacheLineAligned(
        size_t sz, region_id_t rid, bool should_log) const;
            
    region_id_t findOrCreatePRegion(const char *name, int flags,
                                    int *is_created = nullptr);
    region_id_t findPRegion(const char *name, int flags,
                            bool is_in_recovery = false);
    region_id_t createPRegion(const char *name, int flags);
    void     closePRegion(region_id_t, bool is_deleting = false);
    void deletePRegion(const char *name);
    void deleteForcefullyPRegion(const char *name);
    void deleteForcefullyAllPRegions();

    void *getPRegionRoot(region_id_t) const;
    void  setPRegionRoot(region_id_t, void *new_root) const;

    PRegion* searchPRegion(const char *name) const;

    // TODO the following should take the size into consideration
    std::pair<void* /* base address */,region_id_t>
    ensurePRegionMapped(void *addr);

    void dumpDebugInfo() const;
private:
    // PRegionMgr is logically transient
    void *PRegionTable_; // pointer to regions metadata
    int   PRegionTableFD_; // file holding the metadata
    pthread_mutex_t PRegionTableLock_; // mediator across threads
    std::atomic<PRegionExtentMap*> ExtentMap_; // region extent tracker
    
    enum OpType { kCreate_, kFind_, kClose_, kDelete_ };
        
    PRegionMgr() : PRegionTable_{nullptr}, PRegionTableFD_{-1},
        ExtentMap_{new PRegionExtentMap()}
        { pthread_mutex_init(&PRegionTableLock_, NULL); }

    ~PRegionMgr() { delete ExtentMap_.load(std::memory_order_relaxed); }

    PRegionMgr(const PRegionMgr&) = delete;
    PRegionMgr(PRegionMgr&&) = delete;
    PRegionMgr& operator=(const PRegionMgr&) = delete;
    PRegionMgr& operator=(PRegionMgr&&) = delete;
    
    void acquireTableLock()
        { pthread_mutex_lock(&PRegionTableLock_); }
    void releaseTableLock()
        { pthread_mutex_unlock(&PRegionTableLock_); }

    // Mediates metadata management across processes (advisory locking)
    void acquireExclusiveFLock()
        { flock(PRegionTableFD_, LOCK_EX); }
    void releaseFLock()
        { flock(PRegionTableFD_, LOCK_UN); }

    uint32_t getNumPRegions() const
        { return *(static_cast<uint32_t*>(PRegionTable_)); }
            
    void setNumPRegions(uint32_t);

    void *computePRegionBaseAddr(void *addr) const;
            
    PRegion *instantiateNewPRegion(region_id_t rid) const {
        return reinterpret_cast<PRegion*>(
            static_cast<char*>(PRegionTable_) +
            sizeof(uint32_t) + rid * sizeof(PRegion));
    }
    
    void *computeNewPRegionBaseAddr() const;

    PRegion *getPRegionArrayPtr() const;
            
    void initPRegionTable();
    void shutPRegionTable();

    void setCacheParams();
    int getCacheLineSize() const;
    
    void initPRegionRoot(PRegion*);

    region_id_t initNewPRegionImpl(const char *name, int flags);
    region_id_t mapNewPRegion(const char *name, int flags, void *base_addr);
    void mapNewPRegionImpl(
        PRegion *rgn, const char *name, region_id_t rid,
        int flags, void *base_addr);
    void initExistingPRegionImpl(PRegion *preg, const char *name, int flags);
    void mapExistingPRegion(PRegion *preg, const char *name, int flags);
    int mapFile(const char *name, int flags, void *base_addr, bool does_exist);

    void deleteForcefullyPRegion(PRegion*);
    
    void insertExtent(void *first_addr, void *last_addr, region_id_t rid);
    void deleteExtent(void *first_addr, void *last_addr, region_id_t rid);

    void tracePRegion(region_id_t, OpType) const;
    void statsPRegion(region_id_t) const;
};

inline void *PRegionMgr::allocMem(
    size_t sz, region_id_t rid, bool does_need_cache_line_alignment,
    bool does_need_logging) const
{
    return getPRegion(rid)->allocMem(
        sz, does_need_cache_line_alignment, does_need_logging);
}

inline void *PRegionMgr::callocMem(
    size_t nmemb, size_t sz, region_id_t rid) const
{
    return getPRegion(rid)->callocMem(nmemb, sz);
}

inline void *PRegionMgr::reallocMem(
    void *ptr, size_t sz, region_id_t rid) const
{
    return getPRegion(rid)->reallocMem(ptr, sz);
}

inline void *PRegionMgr::allocMemWithoutLogging(
    size_t sz, region_id_t rid) const
{
    bool does_need_cache_line_alignment = false;
    bool does_need_logging = false;
    return allocMem(
        sz, rid, does_need_cache_line_alignment, does_need_logging);
}

inline void *PRegionMgr::allocMemCacheLineAligned(
    size_t sz, region_id_t rid, bool should_log) const
{
    bool does_need_cache_line_alignment = true;
    return allocMem(
        sz, rid, does_need_cache_line_alignment, should_log);
}

inline PRegion *PRegionMgr::getPRegionArrayPtr() const
{
    return reinterpret_cast<PRegion*>(
        static_cast<char*>(PRegionTable_) + sizeof(uint32_t));
}

inline void *PRegionMgr::getPRegionRoot(region_id_t rid) const
{
    return getPRegion(rid)->getRoot();
}

///
/// Given an address, return the base address of the corresponding
/// persistent region
///    
inline void *PRegionMgr::computePRegionBaseAddr(void *addr) const
{
    assert(addr >= (static_cast<char*>(PRegionTable_) + kPRegionSize_) &&
           addr < (static_cast<char*>(PRegionTable_) +
                   kPRegionSize_ * (kMaxNumPRegions_ + 1)) &&
           "Location not in any persistent region!");
    return static_cast<char*>(PRegionTable_) + kPRegionSize_ * 
        ((reinterpret_cast<uint64_t>(addr) -
          reinterpret_cast<uint64_t>(PRegionTable_))/kPRegionSize_);
}

///
/// Compute the base address of the next new persistent region
///    
inline void *PRegionMgr::computeNewPRegionBaseAddr() const
{
    uint32_t num_regions = getNumPRegions();
    if (!num_regions) return (char*)kPRegionsBase_ + kPRegionSize_;
    else return static_cast<char*>(
        reinterpret_cast<PRegion*>(
            reinterpret_cast<char*>(getPRegionArrayPtr()) +
            (num_regions - 1) * sizeof(PRegion))->
        get_base_addr()) + kPRegionSize_;
}
    
///
/// Used with the debug flag below
///
/// Serial mode only or if appropriate locks are held
///    
inline void PRegionMgr::dumpDebugInfo() const
{
#if defined(ATLAS_ALLOC_DUMP)    
    assert(Instance_ && "Region manager does not exist!");
    std::cout << "------- Start of region information -------" << std::endl;
    std::cout << "Region table base: " << PRegionTable_ << std::endl;
    PRegion *pregion_arr_ptr = getPRegionArrayPtr();
    uint32_t curr = 0;
    for (; curr < getNumPRegions(); ++curr, ++pregion_arr_ptr)
        pregion_arr_ptr->dumpDebugInfo();
    std::cout << "------- End of region information -------" << std::endl;
#endif    
}
    
///
/// Used with the trace flag below
///
/// Serial mode only or if appropriate locks are held
///    
inline void PRegionMgr::tracePRegion(region_id_t rid, OpType op) const
{
#if defined(ATLAS_ALLOC_TRACE)
    PRegion *rgn = getPRegion(rid);
    std::cout << "[Atlas] " << 
        (op == kCreate_ ? "Created " :
         op == kFind_   ? "Found " :
         op == kClose_  ? "Closed " :
         op == kDelete_ ? "Deleted " :
         "Unknown")
              << "region with name=" << rgn->get_name()
              << " id " << rgn->get_id() << " address " << rgn->get_base_addr()
              << std::endl;
#endif    
}

///
/// Used with the stats flag below
///
/// Serial mode only or if appropriate locks are held
///
inline void PRegionMgr::statsPRegion(region_id_t rid) const
{
#if defined(ATLAS_ALLOC_STATS)
    getPRegion(rid)->printStats();
#endif    
}


} // namespace Atlas
    
#endif
