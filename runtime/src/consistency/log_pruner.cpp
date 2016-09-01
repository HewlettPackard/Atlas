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
 

#include "atlas_alloc.h"

#include "consistency_mgr.hpp"
#include "helper.hpp"

namespace Atlas {

extern uint64_t removed_log_count;

bool CSMgr::areLogicallySame(LogStructure *gh, LogStructure *cand_gh)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    while (gh) {
        if (gh == cand_gh) return true;
        gh = gh->Next;
    }
    return false;
}

uint32_t CSMgr::getNumNewEntries(LogStructure *new_e, LogStructure *old_e)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    uint32_t new_count = 0;
    uint32_t old_count = 0;
    while (new_e) {
        ++new_count;
        new_e = new_e->Next;
    }
    while (old_e) {
        ++old_count;
        old_e = old_e->Next;
    }
    assert(!(new_count < old_count));
    return new_count - old_count;
}

void CSMgr::destroyLogs(Helper::LogVersions *log_v)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    // Nothing to do
    if (!log_v->size()) return;

#if defined(NVM_STATS) && defined(_PROFILE_HT)
    uint64_t start_log_prune = atlas_rdtsc();
#endif

    LogIterVec deleted_lsp;
    LogEntryVec deleted_log_entries;
    
    LogStructure *cand_gh = 0;
    Helper::LogVersions::iterator logs_ci_end = log_v->end();
    Helper::LogVersions::iterator logs_ci = log_v->begin();
    Helper::LogVersions::iterator last_logs_ci = logs_ci_end;

#if defined(_FLUSH_GLOBAL_COMMIT) && !defined(DISABLE_FLUSHES) && \
    !defined(_DISABLE_DATA_FLUSH)
    bool did_cas_succeed = true;
#endif

    // Go through each version
    while (logs_ci != logs_ci_end) {
        // Get the current pointer to the log
        LogStructure *gh = !CSMgr::getInstance().isInRecovery() ?
            LogMgr::getInstance().getLogPointer(std::memory_order_acquire) :
            LogMgr::getInstance().getRecoveryLogPointer(
                std::memory_order_acquire);
        assert(gh);

        LogStructure *tmp_gh = gh;

        // This is the candidate pointer to the log representing the
        // new consistent state. We would like to move the log pointer
        // to this candidate in a single pointer switch
        // (failure-atomically), essentially moving the program state
        // to the next consistent state.
        cand_gh = (*logs_ci).LS_;
        assert(cand_gh);
        LogStructure *tmp_cand_gh = cand_gh;

        const Log2Bool& deletable_logs = (*logs_ci).Del_;
        
        // Initially, gh is not part of log_v. But if a new gh is formed,
        // gh may actually be the same as cand_gh. In such a case, we just
        // need to skip to the next candidate. Additionally, we need to
        // check for logical identity, meaning that if logs for a new thread
        // were added in between "setting the GH last by the helper thread"
        // and "setting the GH by an application thread because of a new
        // thread creation", we need to identify that scenario as well.
        if (areLogicallySame(gh, cand_gh)) {
            last_logs_ci = logs_ci;
            ++logs_ci;
            continue;
        }
        
        // Since the time of creation of cand_gh, entries may have been
        // added at the head of gh. By comparing the number of
        // *threads*, we can tell whether this has indeed happened. If
        // yes, the new entries are always found at the head.
        uint32_t num_new_entries = getNumNewEntries(tmp_gh, tmp_cand_gh);

        // We can't just use "deletable_logs" to collect logs for
        // removal since the elements in "deletable_logs" may have
        // been split into different versions. For removal purposes,
        // we need to collect for only the versions that can be
        // removed. 
        LogEntryVec tmp_deleted_log_entries;
        LSVec new_entries;
        while (tmp_gh) {
            if (num_new_entries) {
                new_entries.push_back(tmp_gh);
                tmp_gh = tmp_gh->Next;
                // Do not advance tmp_cand_gh
                --num_new_entries;
                continue;
            }
                
            LogEntry *curr_le = tmp_gh->Le;

            assert(tmp_cand_gh);
            LogEntry *end_le = tmp_cand_gh->Le;
            assert(end_le);

            do
            {
                assert(curr_le);

                // Both creation/destruction of threads during log
                // pruning are supported.
                
                // Check whether this log entry is scheduled to be
                // deleted. The cumulative list of deletable
                // log entries is found for every version and that is
                // used to check here. 

                if (deletable_logs.find(curr_le) == deletable_logs.end())
                    break;

                // Add it tentatively to the list of logs to be
                // deleted
                tmp_deleted_log_entries.push_back(curr_le);
                
                // This Next ptr has been read before, so no atomic
                // operation is required.
                curr_le = curr_le->Next;
            }while (curr_le != end_le);

            // We compare the number of headers in GH and cand_GH and
            // if unequal, the difference at the head of GH are the
            // new entries. For this to work, insertions must always
            // be made at the head. 
            tmp_gh = tmp_gh->Next;
            tmp_cand_gh = tmp_cand_gh->Next;
        }

        // If new_entries have been found, we need to add those entries
        // to this version before deleting it.

        if (new_entries.size()) {
            fixupNewEntries(&cand_gh, new_entries);
            (*logs_ci).LS_ = cand_gh;
        }

#if defined(_FLUSH_GLOBAL_COMMIT) && !defined(DISABLE_FLUSHES) && \
        !defined(_DISABLE_DATA_FLUSH)
        if (did_cas_succeed) flushGlobalCommit(tmp_deleted_log_entries);
#endif

        // Atomically flip the global log structure header pointer

        // Note that cache line flushing for this CAS is tricky. The current
        // ad-hoc solution is for user-threads to flush this
        // location after reading it. Since reads of this location are
        // rare enough, this should not hurt performance.
        bool status = !CSMgr::getInstance().isInRecovery() ?
            LogMgr::getInstance().cmpXchngWeakLogPointer(
                gh, cand_gh,
                std::memory_order_acq_rel, std::memory_order_relaxed) :
            LogMgr::getInstance().cmpXchngWeakRecoveryLogPointer(
                gh, cand_gh,
                std::memory_order_acq_rel, std::memory_order_relaxed);
        
        if (status)
        {
#if !defined(_DISABLE_LOG_FLUSH) && !defined(DISABLE_FLUSHES)
            if (!CSMgr::getInstance().isInRecovery())
                LogMgr::getInstance().flushLogPointer();
            else LogMgr::getInstance().flushRecoveryLogPointer();
#endif
            
#if defined(_FLUSH_GLOBAL_COMMIT) && !defined(DISABLE_FLUSHES) && \
    !defined(_DISABLE_DATA_FLUSH)
            did_cas_succeed = true;
#endif            
            // During the first time through the top-level vector elements,
            // gh is not in log_v. So this special handling must be done
            // where we are destroying the old gh that is not in the vector.
            if (logs_ci == log_v->begin()) destroyLS(gh);
            else {
                assert(last_logs_ci != logs_ci_end);
                deleted_lsp.push_back(last_logs_ci);
            }

            // This builds the list of log entries which *will* be
            // deleted
            copyDeletedLogEntries(&deleted_log_entries,
                                  tmp_deleted_log_entries);
            last_logs_ci = logs_ci;
            ++logs_ci;
        }
#if defined(_FLUSH_GLOBAL_COMMIT) && !defined(DISABLE_FLUSHES) && \
    !defined(_DISABLE_DATA_FLUSH)
        else did_cas_succeed = false;
#endif        
        // If the above CAS failed, we don't advance the version but
        // instead try again.
    }

    // Once the root GH pointer has been swung, it does not matter when
    // the log entries are removed or the other stuff is removed. No one can
    // reach these any more.
    destroyLogEntries(deleted_log_entries);
    
    // No need for any cache flushes for any deletions.
    
    LogIterVec::iterator del_ci_end = deleted_lsp.end();
    LogIterVec::iterator del_ci = deleted_lsp.begin();
    LogIterVec::iterator last_valid_ci;
    for (; del_ci != del_ci_end; ++ del_ci)
    {
        destroyLS((**del_ci).LS_);
        last_valid_ci = del_ci;
    }

    del_ci = deleted_lsp.begin();
    if (del_ci != del_ci_end)
    {
        Helper::LogVersions::iterator lvi = *last_valid_ci;
        assert(lvi != log_v->end());
        ++lvi;
        (*log_v).erase(*del_ci, lvi);
    }
#if defined(NVM_STATS) && defined(_PROFILE_HT)
    uint64_t stop_log_prune = atlas_rdtsc();
    Helper::getInstance().incrementTotalPruneTime(
        stop_log_prune - start_log_prune);
#endif
}

void CSMgr::destroyLS(LogStructure *lsp)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    assert(lsp);
    if (!CSMgr::getInstance().isInRecovery())
        assert(lsp != LogMgr::getInstance().getLogPointer(
                   std::memory_order_acquire));
    else assert(lsp != LogMgr::getInstance().getRecoveryLogPointer(
                    std::memory_order_acquire));

    traceHelper("[Atlas] Destroying log header ");
    
    while (lsp)
    {
        LogStructure *del_lsp = lsp;
        lsp = lsp->Next;

        traceHelper(del_lsp);

        // TODO freeMem should call destructor. Use NVM_Destroy
        if (!CSMgr::getInstance().isInRecovery())
            PRegionMgr::getInstance().freeMem(del_lsp, true /* do not log */);
    }
    traceHelper('\n');
}

void CSMgr::destroyLogEntries(const LogEntryVec& le_vec)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    traceHelper("[Atlas] Destroying log entries ");

    LogEntryVec::const_iterator ci_end = le_vec.end();
    for (LogEntryVec::const_iterator ci = le_vec.begin(); ci != ci_end; ++ ci)
    {
        traceHelper(*ci);
        
        if ((*ci)->isRelease() || (*ci)->isRWLockUnlock() || (*ci)->isFree())
        {
            // Add it to a helper map so that the helper elides any
            // happens-after relation from a later-examined log entry
            // to this one 
            Helper::getInstance().addEntryToDeletedMap(*ci, (*ci)->Size);

            // Update the logger table that tracks happens-after
            // between log entries
            if (!CSMgr::getInstance().isInRecovery()) LogMgr::getInstance().deleteOwnerInfo(*ci);
        }

        if (!CSMgr::getInstance().isInRecovery())
        {
#if defined(_LOG_WITH_MALLOC)
            if ((*ci)->isMemop() || (*ci)->isStrop())
                free((void*)(*ci)->ValueOrPtr);
            free(*ci);
#elif defined(_LOG_WITH_NVM_ALLOC)
            if ((*ci)->isMemop() || (*ci)->isStrop())
                PRegionMgr::getInstance().freeMem((void*)(*ci)->ValueOrPtr, true);
            PRegionMgr::getInstance().freeMem(*ci, true /* do not log */);
#else        
            if ((*ci)->isMemop() || (*ci)->isStrop())
                PRegionMgr::getInstance().freeMem((void*)(*ci)->ValueOrPtr, true);
            // TODO cache LogMgr instance
            LogMgr::getInstance().deleteEntry(*ci);
#endif
        }
        ++removed_log_count;
    }

    traceHelper('\n');
}

void CSMgr::copyDeletedLogEntries(LogEntryVec *deleted_les,
                                  const LogEntryVec & tmp_deleted_les)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    LogEntryVec::const_iterator ci_end = tmp_deleted_les.end();
    for (LogEntryVec::const_iterator ci = tmp_deleted_les.begin();
         ci != ci_end; ++ ci)
        (*deleted_les).push_back(*ci);
}

void CSMgr::fixupNewEntries(LogStructure **cand, const LSVec & new_entries)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    LogStructure *new_header = 0;
    LogStructure *last_ls = 0;
    LSVec::const_iterator ci_end = new_entries.end();

    traceHelper("[Atlas] Fixup: created header nodes: ");
    for (LSVec::const_iterator ci = new_entries.begin(); ci != ci_end; ++ci)
    {
        LogStructure *ls = (LogStructure *)
            nvm_alloc(sizeof(LogStructure),
                      LogMgr::getInstance().getRegionId());
        assert(ls);

        traceHelper(ls);
        traceHelper(" ");
        
        ls->Le = (*ci)->Le;
#if !defined(_DISABLE_LOG_FLUSH) && !defined(DISABLE_FLUSHES)
        NVM_FLUSH_ACQ(ls);
#endif
        if (!new_header) new_header = ls;
        else 
        {
            assert(last_ls);
            last_ls->Next = ls;
#if !defined(_DISABLE_LOG_FLUSH) && !defined(DISABLE_FLUSHES)
            NVM_FLUSH_ACQ(&last_ls->Next);
#endif            
        }
        last_ls = ls;
    }

    traceHelper('\n');
    
    last_ls->Next = *cand;
#if !defined(_DISABLE_LOG_FLUSH) && !defined(DISABLE_FLUSHES)
    NVM_FLUSH_ACQ(last_ls);
#endif
    (*cand) = new_header;
}

// TODO: the region may have been closed by this point. One solution is
// to check whether the address belongs to an open region before every
// clflush operation. This would slow down the helper thread but probably
// does not matter. Additionally, more logic needs to be employed in the
// helper thread in the global-flush mode so that if an address in a closed
// region is seen, the helper thread has to stop since no new consistent
// state can be found. This is tied with the observation that in this
// global mode, the consistent state cannot be moved forward during the
// recovery phase.
#if defined(_FLUSH_GLOBAL_COMMIT) && !defined(DISABLE_FLUSHES)
// This is not thread-safe. Currently, only the serial helper thread
// can call this interface.
void CSMgr::flushGlobalCommit(const LogEntryVec& logs)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    assert(GlobalFlush_);
    assert(GlobalFlush_->empty());
    LogEntryVec::const_iterator ci_end = logs.end();
    for (LogEntryVec::const_iterator ci = logs.begin(); ci != ci_end; ++ci)
        if ((*ci)->isStr() || (*ci)->isMemop() || (*ci)->isStrop())
            LogMgr::getInstance().collectCacheLines(
                GlobalFlush_, (*ci)->Addr, (*ci)->Size);
    LogMgr::getInstance().flushCacheLines(*GlobalFlush_);
    GlobalFlush_->clear();
}

#endif

template<class T>
void LogMgr::deleteSlot(CbLog<T> *cb, T *addr)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    assert(!cb->isEmpty());
    assert(addr == &(cb->LogArray[cb->Start.load(std::memory_order_acquire)]));
    cb->Start.store(
        (cb->Start.load(std::memory_order_acquire)+1) % cb->Size,
        std::memory_order_release);
}

template<class T>
void LogMgr::deleteEntry(const std::atomic<CbListNode<T>*> & cb_list, T *addr)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    // Instead of searching every time, we keep track of the last Cb used.
    static CbListNode<T> *last_cb_used = 0;
    if (last_cb_used &&
        ((uintptr_t)addr >= (uintptr_t)last_cb_used->StartAddr &&
         (uintptr_t)addr <= (uintptr_t)last_cb_used->EndAddr)) {
        deleteSlot<T>(last_cb_used->Cb, addr);
        if (last_cb_used->Cb->isEmpty() &&
            last_cb_used->Cb->isFilled.load(std::memory_order_acquire))
            last_cb_used->isAvailable.store(true, std::memory_order_release);
        return;
    }
    CbListNode<T> *curr = cb_list.load(std::memory_order_acquire);
    assert(curr);
    while (curr) {
        if ((uintptr_t)addr >= (uintptr_t)curr->StartAddr &&
            (uintptr_t)addr <= (uintptr_t)curr->EndAddr) {
            last_cb_used = curr;
            deleteSlot<T>(curr->Cb, addr);
            // A buffer that got filled and then emptied implies that
            // the user thread moved on to a new buffer. Hence, mark it
            // available so that it can be reused.
            if (curr->Cb->isEmpty() &&
                curr->Cb->isFilled.load(std::memory_order_acquire))
                curr->isAvailable.store(true, std::memory_order_release);
            break;
        }
        curr = curr->Next;
    }
}

} // namespace Atlas
