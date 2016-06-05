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
 

#ifndef LOG_MGR_HPP
#define LOG_MGR_HPP

#include <cassert>
#include <atomic>
#include <vector>

#include <stdint.h>
#include <pthread.h>

#include "atlas_api.h"

#include "pregion_configs.hpp"
#include "pregion_mgr.hpp"
#include "log_configs.hpp"
#include "log_structure.hpp"
#include "happens_before.hpp"
#include "cache_flush_configs.hpp"
#include "circular_buffer.hpp"
#include "log_elision.hpp"
#include "stats.hpp"

#include "util.hpp"

namespace Atlas {

void * helper(void*);
    
typedef std::vector<LogEntry*> LogEntryVec;

class LogMgr {
    static LogMgr *Instance_;
public:

    // serial mode only
    static LogMgr& createInstance() {
        assert(!Instance_);
        Instance_ = new LogMgr();
        Instance_->init();
        return *Instance_;
    }

    // serial mode only
    static LogMgr& createRecoveryInstance() {
        assert(!Instance_);
        Instance_ = new LogMgr();
        return *Instance_;
    }
    
    // serial mode only
    static void deleteInstance() {
        if (Instance_) {
            Instance_->finalize();
            delete Instance_;
            Instance_ = nullptr;
        }
    }

    static LogMgr& getInstance() {
        assert(Instance_);
        return *Instance_;
    }

    static bool hasInstance() {
        if (!Instance_) return false;
        return Instance_->IsInitialized_;
    }

    void setRegionId(region_id_t id) { RegionId_ = id; }
    region_id_t getRegionId() const { return RegionId_; }

    void acquireLogReadyLock()
        { int status = pthread_mutex_lock(&HelperLock_); assert(!status); }
    void releaseLogReadyLock()
        { int status = pthread_mutex_unlock(&HelperLock_); assert(!status); }
    void waitLogReady() {
        int status = pthread_cond_wait(&HelperCondition_, &HelperLock_);
        assert(!status);
    }
    void signalLogReady()
        { int status = pthread_cond_signal(&HelperCondition_); assert(!status); }

    bool cmpXchngWeakLogPointer(LogStructure *expected,
                                LogStructure *desired,
                                std::memory_order success,
                                std::memory_order failure) {
        return LogStructureHeaderPtr_->compare_exchange_weak(
            expected, desired, success, failure);
    }
    bool cmpXchngWeakRecoveryLogPointer(LogStructure *expected,
                                        LogStructure *desired,
                                        std::memory_order success,
                                        std::memory_order failure) {
        return RecoveryTimeLsp_.compare_exchange_weak(
            expected, desired, success, failure);
    }
    
    // Log creation
    void logNonTemporal(
        LogEntry *le, void *addr, size_t sz, LogType le_type);
    void logAcquire(void*);
    void logRelease(void*);
    void logRdLock(void*);
    void logWrLock(void*);
    void logRWUnlock(void*);
    void logBeginDurable();
    void logEndDurable();
    void logStore(void *addr, size_t sz);
    void logMemset(void *addr, size_t sz);
    void logMemcpy(void *dst, size_t sz);
    void logMemmove(void *dst, size_t sz);
    void logStrcpy(void *dst, size_t sz);
    void logStrcat(void *dst, size_t sz);
    void logAlloc(void *addr);
    void logFree(void *addr);

    LogStructure *createLogStructure(LogEntry *le);

    // Cache flush handling
    // TODO separate flush object
    void psync(void *start_addr, size_t sz);
    void psyncWithAcquireBarrier(void *start_addr, size_t sz);
    void asyncLogFlush(void *p);
    void syncLogFlush();

    void asyncDataFlush(void *p);
    void asyncMemOpDataFlush(void *dst, size_t sz);
    void syncDataFlush();

    void flushAtEndOfFase();
    void collectCacheLines(SetOfInts *cl_set, void *addr, size_t sz);
    void flushCacheLines(const SetOfInts & cl_set);
    void flushCacheLinesUnconstrained(const SetOfInts & cl_set);
    void flushLogUncond(void*);
    void flushLogPointer() { NVM_FLUSH(LogStructureHeaderPtr_); }
    void flushRecoveryLogPointer() { NVM_FLUSH(&RecoveryTimeLsp_); }
    
    bool areUserThreadsDone() const
        { return AllDone_.load(std::memory_order_acquire) == 1; }

    LogStructure *getRecoveryLogPointer(std::memory_order mem_order) const
        { return RecoveryTimeLsp_.load(mem_order); }
    void setRecoveryLogPointer(LogStructure *log_ptr,
                               std::memory_order mem_order)
        { RecoveryTimeLsp_.store(log_ptr, mem_order); }
    LogStructure *getLogPointer(std::memory_order mem_order) const
        { return (*LogStructureHeaderPtr_).load(mem_order); }

    void deleteOwnerInfo(LogEntry *le);
    void deleteEntry(LogEntry *addr)
        { deleteEntry<LogEntry>(CbLogList_, addr); }

    void acquireStatsLock()
        { assert(Stats_); Stats_->acquireLock(); }
    void releaseStatsLock()
        { assert(Stats_); Stats_->releaseLock(); }
    void printStats() const
        { assert(Stats_); Stats_->print(); }
    
private:
    
    // All member of LogMgr are transient. The logs themselves are persistent
    // and maintained in a persistent region

    ///
    /// The following are shared between threads
    ///

    region_id_t RegionId_; // Persistent region holding the logs

    // This is the helper thread that is created at initialization time
    // and joined at finalization time. It is manipulated by the main thread
    // alone avoiding a race.
    pthread_t HelperThread_;

    // pointer to the list of circular buffers containing the log entries
    std::atomic<CbListNode<LogEntry>*> CbLogList_;

    // This is the topmost pointer to the entire global log structure
    std::atomic<LogStructure*> *LogStructureHeaderPtr_;

    // Same as above but during recovery
    std::atomic<LogStructure*> RecoveryTimeLsp_;

    // indicator whether the user threads are done
    std::atomic<int> AllDone_;

    // Condition variable thru which user threads signal the helper thread
    pthread_cond_t HelperCondition_;

    // Mutex for the above condition variable
    pthread_mutex_t HelperLock_;

    // Used to map a lock address to a pointer to LastReleaseInfo, the
    // structure used to maintain information about the last release
    // of the lock 
    std::atomic<LastReleaseInfo*> ReleaseInfoTab_[kHashTableSize];

    // Used to map a lock address to a pointer to LockReleaseCount
    // that maintains the total number of releases of that lock. Used
    // in log elision analysis.
    std::atomic<LockReleaseCount*> LockReleaseHistory_[kHashTableSize];

    Stats *Stats_;

    bool IsInitialized_;
    
    //
    // Start of thread local members
    //

    // Used to signal helper thread once a certain number of log entries
    // has been created by this thread
    thread_local static uint32_t TL_LogCount_; 

    // TODO this is passed around in the code unnecessarily

    // pointer to the current log circular buffer that is used to
    // satisfy new allocation requests for log entries
    thread_local static CbLog<LogEntry> *TL_CbLog_; 

    // Every time the information in a log entry is over-written (either
    // because it is newly created or because it is repurposed), a
    // monotonically increasing generation number is assigned to
    // it. Since these circular buffers are never de-allocated, there
    // is no way a log entry address can be used by multiple
    // threads. So a thread local generation number suffices.
    thread_local static uint64_t TL_GenNum_;

    // Log tracker pointing to the last log entry of this thread
    thread_local static LogEntry *TL_LastLogEntry_;

    // Count of locks held. A non-zero value indicates that execution is
    // within a Failure Atomic SEction (FASE). POSIX says that if an
    // unlock is attempted on an already-released lock, undefined
    // behavior results. So this simple detection of FASE is sufficient.
    thread_local static intptr_t TL_NumHeldLocks_;

    // A set of locks on which the current thread is conditioned. In
    // other words, the current thread's execution may have to be
    // undone if there is a failure and at least one of those locks
    // has not been released one more time. Used in log elision analysis.
    thread_local static MapOfLockInfo *TL_UndoLocks_;

    // A tracker indicating whether a user thread just executed the
    // first statement that is outside a critical section
    thread_local static bool TL_IsFirstNonCSStmt_;

    // A cache with the current intention
    thread_local static bool TL_ShouldLogNonCSStmt_;

    // Total number of logs created by this thread
    thread_local static uint64_t TL_LogCounter_;

    // Set of cache lines that need to be flushed at end of FASE
    thread_local static SetOfInts *TL_FaseFlushPtr_;

    // Used to track unique address/size pair within a consistent section
    thread_local static SetOfPairs *TL_UniqueLoc_;

#if 0 // unused    
    thread_local static intptr_t TL_LogFlushTab_[kFlushTableSize];
#endif
    
    thread_local static intptr_t TL_DataFlushTab_[kFlushTableSize];

    //
    // End of thread local members
    //

    // TODO ensure all are initialized
    LogMgr() :
        RegionId_{kMaxNumPRegions_},
        CbLogList_{nullptr},
        LogStructureHeaderPtr_{nullptr},
        RecoveryTimeLsp_{nullptr},
        AllDone_{0},
        Stats_{nullptr},
        IsInitialized_{false}
        {
            pthread_cond_init(&HelperCondition_, nullptr);
            pthread_mutex_init(&HelperLock_, nullptr);
        }

    ~LogMgr()
        {
#if defined(_FLUSH_LOCAL_COMMIT)  && !defined(DISABLE_FLUSHES)
            delete TL_FaseFlushPtr_;
            TL_FaseFlushPtr_ = nullptr;
#endif            
        }

    void init();
    void finalize();

    // Given a lock address, get a pointer to the bucket for the last
    // release 
    std::atomic<LastReleaseInfo*> *getLastReleaseRoot(void *addr) {
        return ReleaseInfoTab_ + (
            ((reinterpret_cast<uint64_t>(addr)) >> kShift) &
            kHashTableMask); }

    std::atomic<LockReleaseCount*> *getLockReleaseCountRoot(void *addr) {
        return LockReleaseHistory_ + (
            ((reinterpret_cast<uint64_t>(addr)) >> kShift) &
            kHashTableMask); }

    // Log entry creation functions
    LogEntry *allocLogEntry();
    LogEntry *createSectionLogEntry(
        void *lock_address, LogType le_type);
    LogEntry *createAllocationLogEntry(
        void *addr, LogType le_type);
    LogEntry *createStrLogEntry(
        void * addr, size_t size_in_bits);
    LogEntry *createMemStrLogEntry(
        void *addr, size_t sz, LogType le_type);
    LogEntry *createDummyLogEntry();
    
    void publishLogEntry(
        LogEntry *le);
    void signalHelper();
    void finishAcquire(
        void *lock_address, LogEntry *le);
    void finishRelease(
        LogEntry *le, const MapOfLockInfo& undo_locks);
    void markEndFase(
        LogEntry *le);
    void finishWrite(
        LogEntry * le, void * addr);
    void assertOneCacheLine(LogEntry *le) {
#if !defined(_LOG_WITH_NVM_ALLOC) && !defined(_LOG_WITH_MALLOC)
    // The entire log entry must be on the same cache line
    assert(!PMallocUtil::is_on_different_cache_line(le, LAST_LOG_ELEM(le)));
#endif
    }        
    
    // Happens before tracker
    LastReleaseInfo *getLastReleaseHeader(
        void *lock_address);
    LastReleaseInfo *findLastReleaseOfLock(
        void *hash_address);
    LastReleaseInfo *findLastReleaseOfLogEntry(
        LogEntry *candidate_le);
    void addLogToLastReleaseInfo(
        LogEntry *le, const MapOfLockInfo& undo_locks);
    ImmutableInfo *createNewImmutableInfo(
        LogEntry *le, const MapOfLockInfo& undo_locks, bool is_deleted);
    LastReleaseInfo *createNewLastReleaseInfo(
        LogEntry *le, const MapOfLockInfo& undo_locks);
    void setHappensBeforeForAllocFree(
        LogEntry *le);
    
    // Log elision
    bool tryLogElision(
        void *addr, size_t sz);
    bool doesNeedLogging(
        void *addr, size_t sz);
    bool canElideLogging();
    void addLockReleaseCount(
        void *lock_address, uint64_t count);
    LockReleaseCount *getLockReleaseCountHeader(
        void *lock_address);
    LockReleaseCount *findLockReleaseCount(
        void *lock_address);
    uint64_t removeLockFromUndoInfo(
        void *lock_address);

    bool isAddrSizePairAlreadySeen(
        void *addr, size_t sz);

    // Circular buffer management
    template<class T> CbLog<T> *getNewCb(
        uint32_t size, uint32_t rid, CbLog<T> **log_p,
        std::atomic<CbListNode<T>*> *cb_list_p);
    template<class T> T *getNewSlot(
        uint32_t rid, CbLog<T> **log_p,
        std::atomic<CbListNode<T>*> *cb_list_p);
    template<class T> void deleteEntry(
        const std::atomic<CbListNode<T>*>& cb_list, T *addr);
    template<class T> void deleteSlot(
        CbLog<T> *cb, T *addr);

};

inline void LogMgr::logAcquire(void *lock_address)
{
    LogEntry *le = createSectionLogEntry(lock_address, LE_acquire);
    assert(le);

    finishAcquire(lock_address, le);
}

inline void LogMgr::logRdLock(void *lock_address)
{
    LogEntry *le = createSectionLogEntry(lock_address, LE_rwlock_rdlock);
    assert(le);

    finishAcquire(lock_address, le);
}
    
inline void LogMgr::logWrLock(void *lock_address)
{
    LogEntry *le = createSectionLogEntry(lock_address, LE_rwlock_wrlock);
    assert(le);

    finishAcquire(lock_address, le);
}

inline void LogMgr::logBeginDurable()
{
    LogEntry *le = createSectionLogEntry(NULL, LE_begin_durable);
    assert(le);

    finishAcquire(NULL, le);
}

inline void LogMgr::logStore(void *addr, size_t sz)
{
    if (!NVM_IsInOpenPR(addr, sz/8)) return;
    if (tryLogElision(addr, sz/8)) return;
    LogEntry *le = createStrLogEntry(addr, sz);
    finishWrite(le, addr);
}

inline void LogMgr::logMemset(void *addr, size_t sz)
{
    if (!NVM_IsInOpenPR(addr, sz)) return;
    if (tryLogElision(addr, sz)) return;
    LogEntry *le = createMemStrLogEntry(addr, sz, LE_memset);
    finishWrite(le, addr);
}

inline void LogMgr::logMemcpy(void *dst, size_t sz)
{
    if (!NVM_IsInOpenPR(dst, sz)) return;
    if (tryLogElision(dst, sz)) return;
    LogEntry *le = createMemStrLogEntry(dst, sz, LE_memcpy);
    finishWrite(le, dst);
}

inline void LogMgr::logMemmove(void *dst, size_t sz)
{
    if (!NVM_IsInOpenPR(dst, sz)) return;
    if (tryLogElision(dst, sz)) return;
    LogEntry *le = createMemStrLogEntry(dst, sz, LE_memmove);
    finishWrite(le, dst);
}

inline void LogMgr::logStrcpy(void *dst, size_t sz)
{
    if (!NVM_IsInOpenPR((void *)dst, sz)) return;
    if (tryLogElision((void *)dst, sz)) return;
    LogEntry *le = createMemStrLogEntry((void *)dst, sz, LE_strcpy);
    finishWrite(le, dst);
}

inline void LogMgr::logStrcat(void *dst, size_t sz)
{
    if (!NVM_IsInOpenPR(dst, sz)) return;
    if (tryLogElision(dst, sz)) return;
    LogEntry *le = createMemStrLogEntry(dst, sz, LE_strcat);
    finishWrite(le, dst);
}
    
inline LastReleaseInfo *LogMgr::getLastReleaseHeader(void *lock_address)
{
    std::atomic<LastReleaseInfo*> *table_ptr =
        getLastReleaseRoot(lock_address);
    return (*table_ptr).load(std::memory_order_acquire);
}

inline LockReleaseCount *LogMgr::getLockReleaseCountHeader(void *lock_address)
{
    std::atomic<LockReleaseCount*> *entry =
        getLockReleaseCountRoot(lock_address);
    return (*entry).load(std::memory_order_acquire);
}

inline void LogMgr::flushLogUncond(void *p)
{
#if (!defined(DISABLE_FLUSHES) && !defined(_DISABLE_LOG_FLUSH))
#if defined(_LOG_FLUSH_OPT)
    // TODO: this needs more work. It is incomplete.
    AsyncLogFlush(p);
#else
    NVM_FLUSH(p);
#endif
#endif
}
    
} // namespace Atlas

#endif
