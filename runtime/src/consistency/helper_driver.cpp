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
 

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "atlas_api.h"

#include "helper.hpp"
#include "log_mgr.hpp"
#include "consistency_mgr.hpp"

namespace Atlas {
    
uint64_t removed_log_count = 0;

Helper *Helper::Instance_{nullptr};

void *helper(void *arg_lsp)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
#ifdef _DISABLE_HELPER
    return nullptr;
#endif

    Helper::createInstance();
    Helper::getInstance().doConsistentUpdate(arg_lsp);

    if (!arg_lsp) Helper::getInstance().printStats();

#if defined(_NVM_TRACE) || defined(_NVM_VERBOSE_TRACE)
    std::cout <<
        "[Atlas-log-pruner] Traces written to /tmp/atlas_log_pruner.txt" <<
        std::endl;
#endif
    
    Helper::deleteInstance();

    std::cout << "[Atlas-log-pruner] # log entries removed: " <<
        removed_log_count << std::endl;
    return 0;
}

void Helper::collectRelLogEntries(LogStructure *lsp)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    while (lsp) {
        LogEntry *le = lsp->Le;
        while (le) {
            if (le->isRelease()) // TODO: how about free and other rel types?
                ExistingRelMap_.insert(std::make_pair(le, (uint64_t)le->Size));
            le = le->Next;
        }
        lsp = lsp->Next;
    }
}

void Helper::doConsistentUpdate(void *arg_lsp)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    // During normal execution, the argument is NULL.
    if (arg_lsp)
    {
        fprintf(stderr,
                "[Atlas] Advancing consistent state before undoing ...\n");
        collectRelLogEntries((LogStructure*)arg_lsp);
    }

// Have the helper thread work in _small_ chunks. Given that an
// iteration of the helper thread can be quite expensive, we want to
// make sure that the helper thread can be joined as soon as the user
// thread is done. An alternative is for the user thread to send a signal
// to the helper thread.
    
    do {

        ++IterNum_;

        LogStructure *lsp = 0;

        if (!arg_lsp) {
            LogMgr::getInstance().acquireLogReadyLock();
            // We don't want to check this condition within a loop since we
            // need to do some work. If a spurious wakeup happens and that
            // should be rare, the helper thread may go through a round of
            // analysis that may or may not result in wasted work.
            if (!areUserThreadsDone()) LogMgr::getInstance().waitLogReady();
            LogMgr::getInstance().releaseLogReadyLock();
            
            if (areUserThreadsDone()) break;

            lsp = LogMgr::getInstance().getLogPointer(
                std::memory_order_acquire);
        }
        else if (!LogMgr::getInstance().getRecoveryLogPointer(
                     std::memory_order_acquire)) {
            IsInRecovery_ =  true;
            lsp = (LogStructure*)arg_lsp;
            LogMgr::getInstance().setRecoveryLogPointer(
                lsp, std::memory_order_release);
        }
        else lsp = LogMgr::getInstance().getRecoveryLogPointer(
            std::memory_order_acquire);

        // Can't assert during normal execution since a spurious wakeup
        // may have happened
        if (!arg_lsp && !lsp) continue;

        CSMgr& cs_mgr = CSMgr::createInstance();
        if (IsInRecovery_)
            cs_mgr.set_existing_rel_map(&ExistingRelMap_);
        cs_mgr.doConsistentUpdate(lsp, &LogVersions_, IsInRecovery_);
        
        if (IsInRecovery_ && !cs_mgr.get_num_graph_vertices()) {
            CSMgr::deleteInstance();
            break;
        }
        
        CSMgr::deleteInstance();

    }while (!areUserThreadsDone());
}

bool Helper::isDeletedByHelperThread(LogEntry *le, uint64_t gen_num)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    std::pair<DelIter,DelIter> del_iter = DeletedRelLogs_.equal_range(le);
    for (DelIter di = del_iter.first; di != del_iter.second; ++di) {
        if (di->second == gen_num) {
            DeletedRelLogs_.erase(di);
            // Must return since the iterator is broken by the above erase.
            // Any subsequent use of the iterator may fail.
            return true;
        }
    }
    return false;
}

void Helper::printStats()
{
    // TODO The lock should be owned by Stats, not Logger
#if defined(NVM_STATS) 
    LogMgr::getInstance().acquireStatsLock();
    std::cout << "[Atlas-log-pruner] Thread " << pthread_self() << std::endl;
#ifdef _PROFILE_HT
    std::cout << "[Atlas-log-pruner] " << "Total graph creation cycles: " <<
        TotalGraphBuildTime_ << std::endl;
    std::cout << "[Atlas-log-pruner] " << "Total graph resolution cycles: " <<
        TotalGraphResolveTime_ << std::endl;
    std::cout << "[Atlas-log-pruner] " << "Total log prune cycles: " <<
        TotalPruneTime_ << std::endl;
#endif    
    std::cout << "[Atlas-log-pruner] # iterations: " <<
        Helper::getInstance().get_iter_num() << std::endl;
    std::cout << "[Atlas-log-pruner] # flushes from this thread: " <<
        num_flushes << std::endl;
    LogMgr::getInstance().releaseStatsLock();
#endif
}

} // namespace Atlas

