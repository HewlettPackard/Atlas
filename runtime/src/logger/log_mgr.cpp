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

thread_local uint32_t LogMgr::TL_LogCount_{0};
thread_local CbLog<LogEntry> *LogMgr::TL_CbLog_{nullptr};
thread_local uint64_t LogMgr::TL_GenNum_{0};
thread_local LogEntry *LogMgr::TL_LastLogEntry_{nullptr};
thread_local intptr_t LogMgr::TL_NumHeldLocks_{0};
thread_local MapOfLockInfo *LogMgr::TL_UndoLocks_{nullptr};
thread_local bool LogMgr::TL_IsFirstNonCSStmt_{true};
thread_local bool LogMgr::TL_ShouldLogNonCSStmt_{true};
thread_local uint64_t LogMgr::TL_LogCounter_{0};
#if defined(_FLUSH_LOCAL_COMMIT)  && !defined(DISABLE_FLUSHES)
    thread_local SetOfInts *LogMgr::TL_FaseFlushPtr_{new SetOfInts};
#else
    thread_local SetOfInts *LogMgr::TL_FaseFlushPtr_{nullptr};
#endif
#if defined(_OPT_UNIQ_LOC)
    thread_local SetOfPairs *LogMgr::TL_UniqueLoc_{new SetOfPairs};
#else
    thread_local SetOfPairs *LogMgr::TL_UniqueLoc_{nullptr};
#endif

#if 0 // unused
thread_local intptr_t LogMgr::TL_LogFlushTab_[kFlushTableSize] = {};
#endif
    
thread_local intptr_t LogMgr::TL_DataFlushTab_[kFlushTableSize] = {};

LogMgr *LogMgr::Instance_{nullptr};

///
/// @brief Initialize the log manager, sets the log root, creates the
/// helper thread. Called by NVM_Initialize which must be called by
/// only one thread.
///    
void LogMgr::init()
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    std::cout << "[Atlas] -- Started --" << std::endl;

    char *log_name = NVM_GetLogRegionName();
    if (NVM_doesLogExist(NVM_GetFullyQualifiedRegionName(log_name))) {
        std::cout <<
            "[Atlas] The program crashed earlier, please run recovery ..." <<
            std::endl;
        std::cout << "[Atlas] -- Finished --" << std::endl;
        exit(0);
    }
    
#ifdef NVM_STATS
    Stats_ = &Stats::createInstance();
#endif
    
    PRegionMgr::createInstance();
    
    RegionId_ = NVM_CreateRegion(log_name, O_RDWR);
    
    free(log_name); // Log naming functions allocate
    
    LogStructureHeaderPtr_ = static_cast<std::atomic<LogStructure*>*>(
        PRegionMgr::getInstance().allocMemWithoutLogging(
            sizeof(std::atomic<LogStructure*>), RegionId_));
    assert(LogStructureHeaderPtr_);
    new (LogStructureHeaderPtr_) std::atomic<LogStructure*>;
    
    (*LogStructureHeaderPtr_).store(0, std::memory_order_release);
    NVM_FLUSH(LogStructureHeaderPtr_);

    // Enough has been initialized
    IsInitialized_ = true;

    // The memory for LogStructureHeaderPtr_ is leaked if there is a
    // failure before assigning the root below.
    NVM_SetRegionRoot(RegionId_, (void *)LogStructureHeaderPtr_);
    
    // create the helper thread here
    int status = pthread_create(&HelperThread_, nullptr,
                                (void *(*)(void *))helper, nullptr);
    assert(!status);
}

///
/// @brief Finalize the log manager, joins the helper thread and does
/// other bookkeeping. Called by NVM_Finalize which must be called by
/// only one thread.
///    
void LogMgr::finalize()
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    acquireLogReadyLock();
    AllDone_.store(1, std::memory_order_release);
    releaseLogReadyLock();
    signalLogReady();

    int status = pthread_join(HelperThread_, nullptr);
    assert(!status);

//    TODO PrintLog();
    
    NVM_DeleteRegion(NVM_GetLogRegionName());

    PRegionMgr::deleteInstance();

#ifdef NVM_STATS    
    Stats_->deleteInstance();
#endif    

    std::cout << "[Atlas] -- Finished --" << std::endl;
    
}

///
/// @brief Signals the helper thread indicating that there are log
/// entries to process
///    
void LogMgr::signalHelper()
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    ++TL_LogCount_;
    if (TL_LogCount_ == kWorkThreshold) {
        int status = pthread_cond_signal(&HelperCondition_);
        assert(!status);
        TL_LogCount_ = 0;
    }
}

///
/// @brief Entry point into log manager for a lock release
/// @param lock_address Address of the lock object to be released
///
void LogMgr::logRelease(void *lock_address)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    // This should really be an assert
    if (TL_NumHeldLocks_ <= 0) return;
    
    LogEntry *le = createSectionLogEntry(lock_address, LE_release);
    assert(le);

#ifndef _NO_NEST    // default
    // Support for log elision: Since this lock is being released,
    // execution need not be predicated on it any more. So stop
    // tracking it.
    uint64_t lock_count = removeLockFromUndoInfo(lock_address);

    // clean up the thread-local table
    canElideLogging();
#endif
    
    finishRelease(le, *TL_UndoLocks_);

#ifndef _NO_NEST    
    // The following must happen after publishing
    addLockReleaseCount(lock_address, lock_count+1);
#endif    
    
    signalHelper();
}
    
void LogMgr::logRWUnlock(void *lock_address)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    LogEntry *le = createSectionLogEntry(lock_address, LE_rwlock_unlock);
    assert(le);

    uint64_t lock_count = removeLockFromUndoInfo(lock_address);

    // clean up the thread-local table
    canElideLogging();
    
    finishRelease(le, *TL_UndoLocks_);

    // The following must happen after publishing
    addLockReleaseCount(lock_address, lock_count+1);
    
    signalHelper();
}

void LogMgr::logEndDurable()
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    LogEntry *le = createSectionLogEntry(NULL, LE_end_durable);
    assert(le);
    
    assert(TL_NumHeldLocks_ > 0);
    -- TL_NumHeldLocks_;

    publishLogEntry(le);
    TL_LastLogEntry_ = le;

    if (!TL_NumHeldLocks_) markEndFase(nullptr);
    signalHelper();
}

void LogMgr::logAlloc(void *addr)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    // TODO: use the arena lock for log elision
    if (tryLogElision(NULL, 0)) return;
    
    LogEntry *le = createAllocationLogEntry(addr, LE_alloc);

#ifndef _NO_NEST    
    // An allocation is currently treated as an acquire operation
    setHappensBeforeForAllocFree(le);
#endif
    
    publishLogEntry(le);

    TL_LastLogEntry_ = le;
}

void LogMgr::logFree(void *addr)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    // TODO: use the arena lock for log elision
    if (tryLogElision(NULL, 0)) return;
    
    LogEntry *le = createAllocationLogEntry(addr, LE_free);

#ifndef _NO_NEST    
    // If there is a previous free, create a happens after link free -> free
    // In that case, the following call will set the Size_ of the new
    // log entry as well
    setHappensBeforeForAllocFree(le);
#endif
    
    publishLogEntry(le);

#ifndef _NO_NEST
    addLogToLastReleaseInfo(le, *new MapOfLockInfo);
#endif

    TL_LastLogEntry_ = le;
}

} // namespace Atlas


