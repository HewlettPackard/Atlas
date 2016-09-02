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
 

#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <utility>

#include <pthread.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include "pregion_mgr.hpp"
#include "log_mgr.hpp"
#include "util.hpp"
#include "fail.hpp"

#ifdef _NVDIMM_PROLIANT
#include "fsync.hpp"
#endif

namespace Atlas {
    
PRegionMgr *PRegionMgr::Instance_{nullptr};

///
/// Entry point for freeing a persistent location
///    
void PRegionMgr::freeMem(void *ptr, bool should_log) const
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    // Correct size unknown at this point since it may be in transient mem
    region_id_t rgn_id = getOpenPRegionId(ptr, 1 /* dummy */);
    if (rgn_id == kInvalidPRegion_) { // transient memory
        free(ptr);
        return;
    }
    freeMemImpl(rgn_id, ptr, should_log);
}

///
/// Entry point for deleting a persistent location
///    
void PRegionMgr::deleteMem(void *ptr, bool should_log) const
{
#ifdef _FORCE_FAIL
    fail_program();
#endif

    // ptr must be in a persistent region
    region_id_t rgn_id = getOpenPRegionId(ptr, 1 /* dummy */);
    freeMemImpl(rgn_id, ptr, should_log);
}

void PRegionMgr::freeMemImpl(
region_id_t rgn_id, void *ptr, bool should_log) const
{
    // Now that we can find out the correct size, assert that all the
    // bytes of the memory location indeed belong to this region
    assert((getOpenPRegionId(
                ptr, PMallocUtil::get_actual_alloc_size(
                    PMallocUtil::get_requested_alloc_size_from_ptr(ptr))) ==
            rgn_id) && "Location to be freed crosses regions!");
    
    PRegion *preg = getPRegion(rgn_id);
    assert((!preg->is_deleted() && preg->is_mapped()) &&
           "Pointer to be freed belongs to a deleted or unmapped region!");
    preg->freeMem(ptr, should_log);
}
    
///
/// Given a persistent region name and corresponding attributes,
/// return its id, creating it if necessary
///    
region_id_t PRegionMgr::findOrCreatePRegion(
    const char *name, int flags, int *is_created)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    assert(name);
    assert(std::strlen(name) < kMaxlen_+1);

    acquireTableLock(); 
    acquireExclusiveFLock();

    PRegion *rgn = searchPRegion(name);
    if (rgn && !rgn->is_deleted()) {
        initExistingPRegionImpl(rgn, name, flags);
        
        releaseFLock();
        releaseTableLock();

        if (is_created) *is_created = false;

        tracePRegion(rgn->get_id(), kFind_);
        statsPRegion(rgn->get_id());
        
        return rgn->get_id();
    }
    else if (rgn) { // previously deleted region
        // reuse id and base_address
        mapNewPRegionImpl(
            rgn, name, rgn->get_id(), flags, rgn->get_base_addr());

        releaseFLock();
        releaseTableLock();

        if (is_created) *is_created = true;

        tracePRegion(rgn->get_id(), kCreate_);
        statsPRegion(rgn->get_id());
        
        return rgn->get_id();
    }
    else {
        region_id_t rgn_id = initNewPRegionImpl(name, flags);
        
        releaseFLock();
        releaseTableLock();

        if (is_created) *is_created = true;

        tracePRegion(rgn_id, kCreate_);
        statsPRegion(rgn_id);
        
        return rgn_id;
    }
}

///
/// Find a persistent region by its name and return its id
///    
region_id_t PRegionMgr::findPRegion(const char *name, int flags,
                                    bool is_in_recovery)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    assert(name);
    assert(std::strlen(name) < kMaxlen_+1);

    acquireTableLock();
    acquireExclusiveFLock();

    PRegion *rgn = searchPRegion(name);
    // If there was a failure earlier, we want to reuse the region entry
    if (!rgn || (rgn->is_deleted() && !is_in_recovery)) {
        releaseFLock();
        releaseTableLock();
        return kInvalidPRegion_;
    }
    else if (rgn->is_deleted() && is_in_recovery) {
        // If there was a failure earlier, we may find a previously
        // deleted region. Reuse id and base address in such a
        // case but it is ok to re-initialize the root.
        mapNewPRegionImpl(
            rgn, name, rgn->get_id(), flags, rgn->get_base_addr());

        releaseFLock();
        releaseTableLock();
    }
    else {
        initExistingPRegionImpl(rgn, name, flags);

        releaseFLock();
        releaseTableLock();
    }
    
    tracePRegion(rgn->get_id(), kFind_);
    statsPRegion(rgn->get_id());
    
    return rgn->get_id();
}

///
/// Create a new persistent region with the given name and attributes
///    
region_id_t PRegionMgr::createPRegion(const char *name, int flags)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    assert(name);
    assert(std::strlen(name) < kMaxlen_+1);

    acquireTableLock();
    acquireExclusiveFLock();

    region_id_t rgn_id = kInvalidPRegion_;
    PRegion *rgn = searchPRegion(name);
    if (rgn && rgn->is_deleted()) {
        // reuse id and base address
        mapNewPRegionImpl(
            rgn, name, rgn->get_id(), flags, rgn->get_base_addr());
        rgn_id = rgn->get_id();
    }
    else if (rgn)
        assert(!rgn && "Region exists, use a different region!");
    else rgn_id = initNewPRegionImpl(name, flags);

    releaseFLock();
    releaseTableLock();

    tracePRegion(rgn_id, kCreate_);
    statsPRegion(rgn_id);
    
    return rgn_id;
}

///
/// Remove the mappings of a persistent region from memory. It cannot
/// be subsequently used without "finding" it again.
///    
void PRegionMgr::closePRegion(region_id_t rid, bool is_deleting)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    if (!is_deleting) {
        acquireTableLock();
        acquireExclusiveFLock();
    }
    
    PRegion *preg = getPRegion(rid);
    assert(preg && "Region to be closed not found!");
    assert((is_deleting || !preg->is_deleted()) &&
           "Region to be closed already deleted!");
    assert(preg->is_mapped() && "Region to be closed not mapped!");

    int status = munmap(preg->get_base_addr(), kPRegionSize_);
    if (status) {
        perror("munmap");
        assert(!status && "munmap of user region failed!");
    }
    preg->set_is_mapped(false);
    close(preg->get_file_desc());
    
    preg->~PRegion();
    
    if (!is_deleting) {
        releaseFLock();
        releaseTableLock();
    }

    tracePRegion(preg->get_id(), kClose_);
    statsPRegion(preg->get_id());
}

///
/// Delete a persistent region by name. All data within it will
/// disappear as well
///    
void PRegionMgr::deletePRegion(const char *name)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    assert(name);
    assert(std::strlen(name) < kMaxlen_+1);
    
    acquireTableLock();
    acquireExclusiveFLock();

    PRegion *preg = searchPRegion(name);
    assert(preg && "Region to be deleted not found!");
    assert(!preg->is_deleted() && "Region to be deleted already deleted!");

    preg->set_is_deleted(true);

    if (preg->is_mapped()) {
        bool is_deleting = true;
        closePRegion(preg->get_id(), is_deleting);
    }

    char *s = NVM_GetFullyQualifiedRegionName(name);
    unlink(s);
#ifdef _NVDIMM_PROLIANT
    char *parent = strdup(s);
    fsync_dir(parent);
    free(parent);
#endif    
    free(s);

    releaseFLock();
    releaseTableLock();

    tracePRegion(preg->get_id(), kDelete_);
}

///
/// Delete a persistent region without considering its attributes
///    
void PRegionMgr::deleteForcefullyPRegion(const char *name)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    assert(name);
    assert(std::strlen(name) < kMaxlen_+1);
    
    acquireTableLock();
    acquireExclusiveFLock();

    PRegion *preg = searchPRegion(name);
    assert(preg && "Region to be deleted forcefully not found!");

    deleteForcefullyPRegion(preg);
    
    releaseFLock();
    releaseTableLock();
}

void PRegionMgr::deleteForcefullyPRegion(PRegion *preg)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    assert(preg);

    preg->set_is_mapped(false);
    preg->set_is_deleted(true);
    char *s = NVM_GetFullyQualifiedRegionName(preg->get_name());
    unlink(s);
#ifdef _NVDIMM_PROLIANT
    char *parent = strdup(s);
    fsync_dir(parent);
    free(parent);
#endif    
    free(s);
}

///
/// Delete all persistent regions without considering their attributes
///    
void PRegionMgr::deleteForcefullyAllPRegions()
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    acquireTableLock();
    acquireExclusiveFLock();

    uint32_t num_entries = getNumPRegions();
    PRegion *preg_ptr = getPRegionArrayPtr();
    uint32_t curr = 0;
    while (curr < num_entries) {
        deleteForcefullyPRegion(preg_ptr);
        ++ curr; ++ preg_ptr;
    }

    releaseFLock();
    releaseTableLock();
}

///
/// Set the root of a region to the provided new root.
///    
void PRegionMgr::setPRegionRoot(region_id_t rid, void *new_root) const
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    // This must act like a release operation so that all prior
    // writes to NVRAM are flushed out. 
    if (LogMgr::hasInstance())
        LogMgr::getInstance().flushAtEndOfFase();

    getPRegion(rid)->setRoot(new_root);
}

//
// end of public interface implementation
//

///
/// Set the number of persistent regions to count
///    
void PRegionMgr::setNumPRegions(uint32_t count)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    assert(count >= 0 && count < kMaxNumPRegions_
           && "Maximum region count exceeded!");
    *(static_cast<uint32_t*>(PRegionTable_)) = count;
    NVM_FLUSH(PRegionTable_);
}
    
///
/// Initialize the metadata for the persistent regions. The metadata
/// itself is persistent and resides at a fixed address.
///    
void PRegionMgr::initPRegionTable()
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    NVM_CreateUserDir();
    NVM_CreateLogDir(); // TODO rename to RegionDir
    
    char *region_table_name = NVM_GetRegionTablePath();

    struct stat stat_buffer;
    bool does_region_table_exist = stat(region_table_name, &stat_buffer) ?
        false : true;

    PRegionTable_ = (void *)kPRegionsBase_;
    PRegionTableFD_ = mapFile(region_table_name, O_RDWR, PRegionTable_,
                              does_region_table_exist);

    // initialize number of entries
    if (!does_region_table_exist) setNumPRegions(0);

    free(region_table_name);
}

///
/// Remove the mappings of the persistent region metadata from memory
///    
void PRegionMgr::shutPRegionTable()
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    int status = munmap(PRegionTable_, kPRegionSize_);
    if (status) {
        perror("munmap");
        assert(!status && "munmap failed!");
    }
}

///
/// The next few routines map a persistent region into a process
/// address space and insert the available address range into the
/// region manager metadata
///    
region_id_t PRegionMgr::initNewPRegionImpl(const char *name, int flags)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    void *base_addr = computeNewPRegionBaseAddr();
    region_id_t rgn_id = mapNewPRegion(name, flags, base_addr);
    return rgn_id;
}
    
region_id_t PRegionMgr::mapNewPRegion(
    const char *name, int flags, void *base_addr)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    uint32_t num_entries = getNumPRegions();
    PRegion *rgn = instantiateNewPRegion(num_entries);

    mapNewPRegionImpl(rgn, name, num_entries, flags, base_addr);

    // Incrementing the number of regions commits the region
    // metadata. If there is a failure before this increment, none of
    // the region creation changes are visible as if they never happened
    setNumPRegions(num_entries + 1);

    return num_entries;
}

// TODO: bug fix: this routine is not failure-atomic. The fix is to
// change the region ctor to set the deleted bit to true. Then once
// all the changes below are done, set the deleted bit to
// false. 
void PRegionMgr::mapNewPRegionImpl(
    PRegion *rgn, const char *name, region_id_t rid,
    int flags, void *base_addr)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    assert(rgn && "To-be-mapped region not found!");

    new (rgn) PRegion(name, rid, base_addr);
    bool does_exist = false;
    char *fully_qualified_name = NVM_GetFullyQualifiedRegionName(name);
    rgn->set_file_desc(
        mapFile(fully_qualified_name, flags, base_addr, does_exist));

    insertExtent(base_addr, (char*)base_addr + kPRegionSize_ - 1, rid);
    
    free(fully_qualified_name);
    
    initPRegionRoot(rgn);
}
    
void PRegionMgr::initExistingPRegionImpl(
    PRegion *preg, const char *name, int flags)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    assert(preg && "Existing region not found!");
    assert(!preg->is_deleted());
    assert(preg->get_base_addr() != nullptr);

    mapExistingPRegion(preg, name, flags);
}
    
void PRegionMgr::mapExistingPRegion(PRegion *preg, const char *name, int flags)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    preg->initArenaTransients();
    PMallocUtil::set_default_tl_curr_arena(preg->get_id());
    
    bool does_exist = true;

    char *fully_qualified_name = NVM_GetFullyQualifiedRegionName(name);
    preg->set_file_desc(mapFile(fully_qualified_name,
                                flags, preg->get_base_addr(), does_exist));

    insertExtent(preg->get_base_addr(),
                 (char*)preg->get_base_addr() + kPRegionSize_ - 1,
                 preg->get_id());
    
    free(fully_qualified_name);

    preg->set_is_mapped(true);
}

int PRegionMgr::mapFile(
    const char *name, int flags, void *base_addr, bool does_exist)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    int fd = open(
        name, does_exist ? flags : flags |
#ifdef _NVDIMM_PROLIANT
        O_DIRECT |
#endif        
        O_CREAT,
        flags == O_RDONLY  ? S_IRUSR : (S_IRUSR | S_IWUSR));
    if (fd == -1) {
        perror("open");
        assert(fd != -1 && "Error opening to-be-mapped file!");
    }

    if (!does_exist) {
        int status = ftruncate(fd, kPRegionSize_);
        assert(!status);
    }

    void *addr = mmap(base_addr, kPRegionSize_,
                       flags == O_RDONLY ? PROT_READ : PROT_READ | PROT_WRITE,
                       MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        assert(addr != MAP_FAILED && "mmap failed!");
    }
    assert(addr == base_addr && "mmap returned address is not as requested!");

#ifdef _NVDIMM_PROLIANT
    if (!does_exist) {
        // Try to pre-allocate storage space
        int allocate_status = posix_fallocate(fd, 0, kPRegionSize_);
        assert(!allocate_status);

        // At least on some kernels, posix_fallocate does not appear to
        // be sufficient. Do a memset to force pre-allocation to make sure
        // all filesystem metadata changes are made.
        memset(addr, 0, kPRegionSize_);

        // Force filesystem metadata changes to backing store
        fsync_paranoid(name);
    }
#endif
    
    return fd;
}

///
/// Initialize the persistent region root
///    
void PRegionMgr::initPRegionRoot(PRegion *preg)
{
    intptr_t *root_ptr = static_cast<intptr_t*>(preg->allocRoot());
    // Flushed but not logged
    *root_ptr = 0;
    NVM_FLUSH(root_ptr);
}

///
/// Given a name, search the persistent region metadata and return a
/// pointer to the corresponding metadata entry if it exists
///    
PRegion* PRegionMgr::searchPRegion(const char *name) const
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    PRegion *pregion_arr_ptr = getPRegionArrayPtr();
    uint32_t curr = 0;
    for (; curr < getNumPRegions(); ++curr, ++pregion_arr_ptr)
        if (!strcmp(name, pregion_arr_ptr->get_name()))
            return pregion_arr_ptr;
    return nullptr;
}

///
/// Given a memory address and a size, return the id of the open
/// persistent region that it belongs to
///    
region_id_t PRegionMgr::getOpenPRegionId(
    const void *addr, size_t sz) const {
    return ExtentMap_.load(std::memory_order_acquire)->findExtent(
        reinterpret_cast<intptr_t>(addr),
        reinterpret_cast<intptr_t>(static_cast<const char*>(addr)+sz-1));
}

///
/// Given an address, make sure that the persistent regions it belongs
/// to is mapped
///    
std::pair<void*,region_id_t> PRegionMgr::ensurePRegionMapped(void *addr)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    region_id_t rgn_id = getOpenPRegionId(addr, 1 /* dummy */);
    if (rgn_id != kInvalidPRegion_)
        return std::make_pair(getPRegion(rgn_id)->get_base_addr(), rgn_id);
    
    PRegion *curr_rgn = getPRegionArrayPtr();
    uint32_t num_rgn = getNumPRegions();
    uint32_t curr = 0;
    for(; curr < num_rgn; ++curr, ++curr_rgn) {
        if (curr_rgn->is_deleted()) continue;

        if (addr >= curr_rgn->get_base_addr() && 
            static_cast<char*>(addr) <
            static_cast<char*>(curr_rgn->get_base_addr())+kPRegionSize_) {
            initExistingPRegionImpl(curr_rgn, curr_rgn->get_name(), O_RDWR);

            tracePRegion(curr_rgn->get_id(), kFind_);
            statsPRegion(curr_rgn->get_id());

            return std::make_pair(
                curr_rgn->get_base_addr(), curr_rgn->get_id());
        }
    }
    assert(0 && "Address does not belong to any persistent region!");
    return std::make_pair(nullptr, kInvalidPRegion_);
}

    
///
/// Add a range of addresses and the corresponding region id to the
/// region manager metadata
///

// The following assumes interference-freedom, i.e. a lock must be held

// TODO Is the old map leaked?
void PRegionMgr::insertExtent(
    void *first_addr, void *last_addr, region_id_t rid)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    PRegionExtentMap *old_map_ptr = ExtentMap_.load(std::memory_order_acquire);
    PRegionExtentMap *new_map_ptr = nullptr;
    do {
        new_map_ptr = new PRegionExtentMap(*old_map_ptr);
        new_map_ptr->insertExtent(reinterpret_cast<intptr_t>(first_addr),
                        reinterpret_cast<intptr_t>(last_addr), rid);
    }while (!ExtentMap_.compare_exchange_weak(
                old_map_ptr, new_map_ptr,
                std::memory_order_acq_rel, std::memory_order_relaxed));
}

/// Delete a range of addresses and the corresponding region id from
/// the region manager metadata
///    

// The following assumes interference-freedom, i.e. a lock must be held

// TODO Is the old map leaked?
void PRegionMgr::deleteExtent(
    void *first_addr, void *last_addr, region_id_t rid)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    PRegionExtentMap *old_map_ptr = ExtentMap_.load(std::memory_order_acquire);
    PRegionExtentMap *new_map_ptr = nullptr;
    do {
        new_map_ptr = new PRegionExtentMap(*old_map_ptr);
        new_map_ptr->deleteExtent(reinterpret_cast<intptr_t>(first_addr),
                                  reinterpret_cast<intptr_t>(last_addr), rid);
    }while (!ExtentMap_.compare_exchange_weak(
                old_map_ptr, new_map_ptr,
                std::memory_order_acq_rel, std::memory_order_relaxed));
}

int PRegionMgr::getCacheLineSize() const
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    int size;
    FILE * fp = fopen("/sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size", "r");
    if (fp) {
        int n = fscanf(fp, "%d", &size);
        assert(n == 1);
        int status = fclose(fp);
        assert(!status);
    }
    else {
        size = kDCacheLineSize_;
        std::cout << "[Atlas] WARNING: Config file not found: Using default cache line size of " << size << " bytes" << std::endl;
    }
    return size;
}

void PRegionMgr::setCacheParams() 
{
    uint32_t cache_line_size = getCacheLineSize();
    PMallocUtil::set_cache_line_size(cache_line_size);
    PMallocUtil::set_cache_line_mask(0xffffffffffffffff - cache_line_size + 1);
}
    
} // namespace Atlas
