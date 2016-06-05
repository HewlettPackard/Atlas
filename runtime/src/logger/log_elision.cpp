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

#include "log_mgr.hpp"

namespace Atlas {

void LogMgr::addLockReleaseCount(void *lock_address, uint64_t count)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    LockReleaseCount *lc = findLockReleaseCount(lock_address);
    if (lc) lc->Count.store(count, std::memory_order_release);
    else {
        LockReleaseCount *new_entry =
            new LockReleaseCount(lock_address, count);
        LockReleaseCount *first_lcp;
        do {
            first_lcp = getLockReleaseCountHeader(lock_address);
            new_entry->Next = first_lcp;
        }while (!getLockReleaseCountRoot(lock_address)->
                compare_exchange_weak(
                    first_lcp, new_entry,
                    std::memory_order_acq_rel, std::memory_order_relaxed));
    }
}

LockReleaseCount *LogMgr::findLockReleaseCount(void *lock_address)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    LockReleaseCount *lcp = getLockReleaseCountHeader(lock_address);
    while (lcp) {
        if (lcp->LockAddr == lock_address) return lcp;
        lcp = lcp->Next;
    }
    return nullptr;
}

uint64_t LogMgr::removeLockFromUndoInfo(void *lock_address)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    assert(TL_UndoLocks_);
    MapOfLockInfo::iterator ci = TL_UndoLocks_->find(lock_address);
    assert(ci != TL_UndoLocks_->end());
    uint64_t lock_count = ci->second;
    TL_UndoLocks_->erase(ci);
    return lock_count;
}

bool LogMgr::canElideLogging()
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    // if the thread-local table of undo locks is not yet created, it
    // means that critical sections haven't been executed by this thread
    // yet. So nothing needs to be undone.
    if (!TL_UndoLocks_) return true;

    typedef std::vector<MapOfLockInfo::iterator> ItrVec;
    ItrVec itr_vec;
    bool ret = true;
    MapOfLockInfo::iterator ci_end = TL_UndoLocks_->end();
    for (MapOfLockInfo::iterator ci = TL_UndoLocks_->begin();
         ci != ci_end; ++ ci) {
        void *lock_address = ci->first;
        uint64_t lock_count = ci->second;
        LockReleaseCount *lc = findLockReleaseCount(lock_address);
        assert(lc);
        if (lc->Count.load(std::memory_order_acquire) <= lock_count) {
            ret = false;
            continue;
        }
        itr_vec.push_back(ci);
    }
    ItrVec::iterator itr_ci_end = itr_vec.end();
    for (ItrVec::iterator itr_ci = itr_vec.begin();
         itr_ci != itr_ci_end; ++ itr_ci)
        TL_UndoLocks_->erase(*itr_ci);
    return ret;
}

bool LogMgr::isAddrSizePairAlreadySeen(void *addr, size_t sz)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    assert(TL_UniqueLoc_);
    if (FindSetOfPairs(*TL_UniqueLoc_, addr, sz) != TL_UniqueLoc_->end())
        return true;
    InsertSetOfPairs(TL_UniqueLoc_, addr, sz);
    return false;
}
    
bool LogMgr::doesNeedLogging(void *addr, size_t sz)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
#if defined(_ALWAYS_LOG)
    return true;
#endif    
    
    // if inside a consistent section, logging can be elided if this
    // address/size pair has been seen before in this section
    if (TL_NumHeldLocks_ > 0) {
        // TODO more evaluation of the following
#ifdef _OPT_UNIQ_LOC
        if (isAddrSizePairAlreadySeen(addr, sz)) {
#ifdef NVM_STATS
            Stats_->incrementUnloggedCriticalSectionStoreCount();
#endif            
            return false;
        }
#endif        
        return true;
    }
    // If we are here, it means that this write is outside a critical section
#if defined(_SRRF) || defined(_NO_NEST)
    return false;
#endif    

    if (TL_IsFirstNonCSStmt_) {
        // Since we end a failure-atomic section at the end of a critical
        // section, this is also the first statement of the failure-
        // atomic section. Hence, this address/size pair couldn't have
        // been seen earlier.
        
        TL_ShouldLogNonCSStmt_ = !canElideLogging();
        TL_IsFirstNonCSStmt_ = false;
        return TL_ShouldLogNonCSStmt_;
    }
    else {
#ifdef _OPT_UNIQ_LOC
        if (TL_ShouldLogNonCSStmt_)
            if (isAddrSizePairAlreadySeen(addr, sz))
                return false;
#endif        
        return TL_ShouldLogNonCSStmt_;
    }
    return true;
}

bool LogMgr::tryLogElision(void *addr, size_t sz)
{
    // TODO warn for the following scenario:
    // If the region table does not exist when we reach here, it means
    // that the compiler instrumented an access that is executed before
    // the region table is initialized. This can happen in legal situations
    // if the compiler cannot prove that this access is to a transient
    // memory location. But it can also happen if this is a synchronization
    // operation. In both these cases, it is ok not to log this operation.
    // But it could also be the result of incorrect programming where the
    // user specifies a non-existent persistent region or something like
    // that. So it is a good idea to warn.
//    if (!region_table_addr) return true;

    // TODO be consistent with the defines
#if defined(_FLUSH_LOCAL_COMMIT) && !defined(DISABLE_FLUSHES) && \
    !defined(_DISABLE_DATA_FLUSH)
    assert(TL_FaseFlushPtr_);
    collectCacheLines(TL_FaseFlushPtr_, addr, sz);
#endif
    if (!doesNeedLogging(addr, sz)) {
#ifdef NVM_STATS
        Stats_->incrementUnloggedStoreCount();
#endif
        return true;
    }
#ifdef NVM_STATS    
    if (!TL_NumHeldLocks_) Stats_->incrementLogElisionFailCount();
#endif    
    return false;
}

} // namespace Atlas
