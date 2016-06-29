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
 

#include "log_mgr.hpp"
#include "log_structure.hpp"
#include "happens_before.hpp"

#include "atlas_alloc.h"

namespace Atlas {

///
/// @brief Given a log entry, publish it to other threads
/// @param le Pointer to the already populated log entry
///    
void LogMgr::publishLogEntry(LogEntry *le)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    // if TL_LastLogEntry_ is null, it means that this is the first
    // log entry created in this thread. In that case, allocate space
    // for the thread-specific header, set a pointer to this log entry,
    // and insert the header into the list of headers.
    if (!TL_LastLogEntry_) {
        LogStructure *ls =
            static_cast<LogStructure*>(
                PRegionMgr::getInstance().allocMemWithoutLogging(
                    sizeof(LogStructure), RegionId_));
        assert(ls);

        new (ls) LogStructure(le, nullptr);
        
        flushLogUncond(le);

        LogStructure *tmp;
        do {
            tmp = (*LogStructureHeaderPtr_).load(std::memory_order_acquire);
            // Ensure that data modified by read-modify-write
            // instructions reach persistent memory before being consumed
            flushLogUncond(LogStructureHeaderPtr_);

            ls->Next = tmp;
            // 16-byte alignment guarantees that an element of type
            // LogStructure is on the same cache line
            assert(!isOnDifferentCacheLine(&ls->Next, &ls->Le));
            flushLogUncond(&ls->Next);

        }while (!LogStructureHeaderPtr_->compare_exchange_weak(
                    tmp, ls,
                    std::memory_order_acq_rel, std::memory_order_relaxed));
        flushLogUncond(LogStructureHeaderPtr_);
    }
    else {
        // "le" will be attached to the thread specific list of log entries

        // In the default mode:
        // If "le" is the first entry in its cache line, flush "le",
        // reset the Next field and flush Next (2 cache line flushes).
        // If "le" is not the first entry in its cache line, assert
        // that *TL_LastLogEntry_ is in the same cache line as le, set
        // TL_LastLogEntry_->Next and flush the corresponding cache
        // line (1 cache line flush). 
#if defined(_USE_MOVNT)
        long long unsigned int *from = (long long unsigned int*)&le;
        __builtin_ia32_movntq(
            (__attribute__((__vector_size__(1*sizeof(long long)))) long long*)
            (long long unsigned int*)&TL_LastLogEntry_->Next,
            (__attribute__((__vector_size__(1*sizeof(long long)))) long long)
            *from);
#if !defined(_NO_SFENCE)            
        __builtin_ia32_sfence();
#endif        
#else            
        if (isCacheLineAligned(le)) {
            flushLogUncond(le);
            TL_LastLogEntry_->Next.store(le, std::memory_order_release);
            flushLogUncond(&TL_LastLogEntry_->Next);
        }
        else {
#if !defined(_LOG_WITH_NVM_ALLOC) && !defined(_LOG_WITH_MALLOC) // default
            assert(!PMallocUtil::is_on_different_cache_line(
                       le, static_cast<void*>(&TL_LastLogEntry_->Next)));
            TL_LastLogEntry_->Next.store(le, std::memory_order_release);
            flushLogUncond(le);
#else
            // TODO: _LOG_WITH_MALLOC may not have proper alignment,
            // requiring more cache line flushes. To be fixed.
            if (PMallocUtil::is_on_different_cache_line(
                    le, (void*)&TL_LastLogEntry_->Next)) {
                flushLogUncond(le);
                TL_LastLogEntry_->Next.store(le, std::memory_order_release);
                flushLogUncond(&TL_LastLogEntry_->Next);
            }
            else {
                TL_LastLogEntry_->Next.store(le, std::memory_order_release);
                flushLogUncond(le);
            }
#endif            
        }
#endif        
    }
}

///
/// @brief After a log entry is created for a lock acquire, perform
/// some other bookkeeping tasks
/// @param lock_address
/// @param le Log entry for the lock acquire
///
void LogMgr::finishAcquire(void *lock_address, LogEntry *le)
{
    assert(TL_NumHeldLocks_ >= 0);

    ++TL_NumHeldLocks_;

#ifdef NVM_STATS
    Stats_->incrementCriticalSectionCount();
    if (TL_NumHeldLocks_ > 1) Stats_->incrementNestedCriticalSectionCount();
#endif

#ifndef _NO_NEST    
    if (lock_address) {
        LockReleaseCount *lcp = findLockReleaseCount(lock_address);
        uint64_t lock_count;
        if (lcp) lock_count = lcp->Count.load(std::memory_order_acquire);
        else {
            lock_count = 0;
            addLockReleaseCount(lock_address, lock_count);
        }
        if (!TL_UndoLocks_) TL_UndoLocks_ = new MapOfLockInfo;
        (*TL_UndoLocks_)[lock_address] = lock_count;

        // Find the log entry corresponding to the release of this lock
        // If null, the inter-thread pointer is left as is
        LastReleaseInfo *oi = findLastReleaseOfLock(lock_address);
        if (oi) {
            ImmutableInfo *ii = oi->Immutable.load(std::memory_order_acquire);
            // TODO: this can be elided if the target belongs to the
            // same thread
            le->ValueOrPtr = (intptr_t)(ii->LogAddr);

            // Set the generation number
            // Note that the release log entry _must_ exist for the
            // assignment below to work.
            LogEntry *rel_le = reinterpret_cast<LogEntry*>(le->ValueOrPtr);
            assert(rel_le->isRelease() || rel_le->isRWLockUnlock() ||
                   rel_le->isFree());
            le->Size = rel_le->Size;
            
            MapOfLockInfo *moli = ii->LockInfoPtr;
            if (moli) (*TL_UndoLocks_).insert((*moli).begin(), (*moli).end());
        }
    }
#endif
    
    publishLogEntry(le);
    TL_LastLogEntry_ = le;
}

void LogMgr::finishRelease(LogEntry *le, const MapOfLockInfo& undo_locks)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    // TODO Fails on wrongly syncrhonized programs
    assert(TL_NumHeldLocks_ > 0);
    -- TL_NumHeldLocks_;
    
    publishLogEntry(le);

#ifndef _NO_NEST    
    addLogToLastReleaseInfo(le, undo_locks);
#endif    

    TL_LastLogEntry_ = le;

    if (!TL_NumHeldLocks_) markEndFase(le);
}

void LogMgr::markEndFase(LogEntry *le)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
#ifdef _OPT_UNIQ_LOC
    if (TL_UniqueLoc_) TL_UniqueLoc_->clear();
#endif    

    flushAtEndOfFase();

    TL_IsFirstNonCSStmt_ = true;

    // Since this is the end of a failure-atomic section, create a
    // dummy log entry. A dummy log entry ensures that there is at least
    // one outstanding log entry even if all other log entries are pruned
    // out. Note that currently we are adding a dummy log entry for
    // every failure-atomic section. This can be costly. Static
    // analysis should be used to elide this dummy log entry if it can
    // prove that there is either a failure-atomic section or a store
    // instruction following this failure-atomic section.
    LogEntry *dummy_le = createDummyLogEntry();
    publishLogEntry(dummy_le);
    TL_LastLogEntry_ = dummy_le;
}

void LogMgr::finishWrite(LogEntry * le, void * addr)
{
    assert(le);

#ifdef NVM_STATS
    Stats_->incrementLoggedStoreCount();
    if (TL_NumHeldLocks_ > 0) Stats_->incrementCriticalLoggedStoreCount();
#endif    
        
    publishLogEntry(le);
    TL_LastLogEntry_ = le;
    signalHelper(); // TODO: why here?
}

} // namespace Atlas
