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
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <assert.h>

#include "util.hpp"
#include "atlas_alloc.h"
#include "pregion_mgr.hpp"
#include "pregion_configs.hpp"
#include "log_mgr.hpp"
#include "recover.hpp"

using namespace Atlas;

//namespace Atlas {
    
Tid2Log first_log_tracker;
Tid2Log last_log_tracker;
MapR2A map_r2a;

// TODO: need to factor in the generation number. Does free with overloaded
// gen_num still work?
MapLog2Bool replayed_entries;
MapInt2Bool done_threads;
MapLog2Log prev_log_mapper;

// All open persistent regions must have an entry in the following data
// structure that maps a region address range to its region id.
// TODO: is this still required with the new persistent region manager?
MapInterval mapped_prs;

uint64_t replayed_count = 0;

//}

int main(int argc, char **argv)
{
    assert(argc == 2);
    
    R_Initialize(argv[1]);
    
    LogStructure *lsp = GetLogStructureHeader();

    // This can happen if logs were never created by the user threads
    // or if the log entry was deleted by the region manager but there
    // was a failure before the log file was removed.
    
    // Note that if logs are ever created, there should be some remnants
    // after a crash since the helper thread never removes everything.
    if (!lsp)
    {
        fprintf(stderr, "[Atlas] Warning: No logs present\n");
        R_Finalize(argv[1]);
        exit(0);
    }

#if !defined(_FLUSH_GLOBAL_COMMIT)
    helper(lsp);
#endif
    
    LogStructure *recovery_lsp =
        LogMgr::getInstance().getRecoveryLogPointer(std::memory_order_acquire);
    if (recovery_lsp) lsp = recovery_lsp;
    
    CreateRelToAcqMappings(lsp);

    Recover();
    
    R_Finalize(argv[1]);
}

void R_Initialize(const char *s)
{
//    NVM_SetupRegionTable(s);

    PRegionMgr::createInstance();
    LogMgr::createRecoveryInstance();
    
    // The exact mechanism to find the regions that need to be reverted
    // remains to be done. One possibility is to look at all logs on
    // persistent memory, conceivably from a certain pre-specified region.
    // For now, the recovery process requires the name of the process
    // whose crash we are trying to recover from. Using this information,
    // the recovery phase constructs the name of the corresponding log and
    // finds the regions needing recovery from this log.
    char *log_name = NVM_GetLogRegionName(s);
    if (!NVM_doesLogExist(NVM_GetFullyQualifiedRegionName(log_name)))
    {
        fprintf(stderr, "[Atlas] No log file exists, nothing to do ...\n");
        free(log_name);
        exit(0);
    }

    bool is_in_recovery = true;
    region_id_t nvm_logs_id =
        PRegionMgr::getInstance().findPRegion(log_name, O_RDWR,
                                              is_in_recovery);
    assert(nvm_logs_id != kInvalidPRegion_ &&
           "Log region not found in region table!");
    
    LogMgr::getInstance().setRegionId(nvm_logs_id);

    void *log_base_addr =
        PRegionMgr::getInstance().getPRegion(
            Atlas::LogMgr::getInstance().getRegionId())->get_base_addr();
    InsertToMapInterval(&mapped_prs,
                        (uint64_t)log_base_addr,
                        (uint64_t)((char*)log_base_addr+kPRegionSize_),
                        Atlas::LogMgr::getInstance().getRegionId());
    free(log_name);
}

// TODO probably want to have a deleteRecoveryInstance
void R_Finalize(const char *s)
{
    MapInterval::const_iterator ci_end = mapped_prs.end();
    for (MapInterval::const_iterator ci = mapped_prs.begin();
         ci != ci_end; ++ ci)
        PRegionMgr::getInstance().closePRegion(ci->second);

    char *log_name = NVM_GetLogRegionName(s);
    NVM_DeleteRegion(log_name);
    free(log_name);
    fprintf(stderr, "[Atlas] Done bookkeeping\n");
}

LogStructure *GetLogStructureHeader()
{
    // TODO use atomics
    LogStructure **lsh_p =
        (LogStructure**)NVM_GetRegionRoot(
            Atlas::LogMgr::getInstance().getRegionId());
    if (!lsh_p) {
        std::cout <<
            "[Atlas] Region root is null: did you forget to set it?"
                  << std::endl;
        return nullptr;
    }
    return (LogStructure*)*lsh_p;
}

void CreateRelToAcqMappings(LogStructure *lsp)
{
    // just use a logical thread id
    int tid = 0;
    // The very first log for a given thread must be found. This will be
    // the last node replayed for this thread. Note that most of the time
    // this first log will point to null but that cannot be guaranteed. The
    // helper thread commits changes atomically by switching the log structure
    // header pointer. The logs are destroyed after this atomic switch.
    while (lsp)
    {
        LogEntry *last_log = 0; // last log written in program order
        LogEntry *le = lsp->Le;
        if (le)
        {
            assert(first_log_tracker.find(tid) == first_log_tracker.end());
            first_log_tracker[tid] = le;
        }
        while (le)
        {
            if (le->isAcquire() || le->isAlloc() || le->isFree())
                AddToMap(le, tid);
            prev_log_mapper[le] = last_log;
            last_log = le;
            le = le->Next;
        }
        if (last_log)
        {
            assert(last_log_tracker.find(tid) == last_log_tracker.end());
            last_log_tracker.insert(make_pair(tid, last_log));
        }
        ++ tid;
        lsp = lsp->Next;
    }
}

void AddToMap(LogEntry *acq_le, int tid)
{
    LogEntry *rel_le = (LogEntry *)(acq_le->ValueOrPtr);

    if (rel_le) {
#if 0 // We can't assert this since a log entry may be reused.
#ifdef DEBUG        
        if (map_r2a.find(rel_le) != map_r2a.end())
        {
            assert(rel_le->isRWLockUnlock());
            assert(acq_le->isRWLockRdLock());
        }
#endif
#endif        
        map_r2a.insert(make_pair(rel_le, make_pair(acq_le, tid)));
    }
}

#ifdef _NVM_TRACE
template<class T> void RecoveryTrace(void *p)
{
    fprintf(stderr, "%p %ld\n", p, *(T*)p);
}
#endif

// The first time an address in a given persistent region (PR) is found,
// that PR is opened and all open PRs must have an entry in mapped_prs.
// Every time an address has to be replayed, it is looked up in the above
// mapper to determine whether it is already mapped. The same mapper data
// structure is used during logging as well to track all open PRs. We assume
// that no transient location is logged since it must have been filtered
// out using this mapper during logging.
void Replay(LogEntry *le)
{
    assert(le);
    assert(le->isStr() || le->isMemop() || le->isAlloc() ||
           le->isFree() || le->isStrop());

    void *addr = le->Addr;
    if (FindInMapInterval(mapped_prs,
                          (uint64_t)addr,
                          (uint64_t)((char*)addr+le->Size-1)) ==
        mapped_prs.end()) {
        pair<void*,uint32_t> mapper_result =
            PRegionMgr::getInstance().ensurePRegionMapped(addr);
        InsertToMapInterval(&mapped_prs,
                            (uint64_t)(char*)mapper_result.first,
                            (uint64_t)((char*)mapper_result.first+kPRegionSize_),
                            mapper_result.second);
    }

    if (le->isStr()) {
        // TODO bit access is not supported?
        assert(!(le->Size % 8));
        memcpy(addr, (void*)&(le->ValueOrPtr), le->Size/8);
    }
    else if (le->isMemop() || le->isStrop()) {
        assert(le->ValueOrPtr);
        memcpy(addr, (void*)(le->ValueOrPtr), le->Size);
    }
    else if (le->isAlloc()) *((size_t*)addr) = false; // undo allocation
    else if (le->isFree())  *((size_t*)addr) = true;  // undo de-allocation
    else assert(0 && "Bad log entry type");
    
    ++ replayed_count;
}

LogEntry *GetPrevLogEntry(LogEntry *le)
{
    assert(le);
    if (prev_log_mapper.find(le) != prev_log_mapper.end())
        return prev_log_mapper.find(le)->second;
    else return NULL;
}

// We start from an arbitrary thread (say, tid 0), grab its last log and
// start playing back. If we hit an acquire node, we do nothing. If we
// hit a release node, we look up the map to see if it has a corresponding
// acquire node. If yes, we switch to this other thread, grab its last
// log and start playing it. This method is followed until all logs have
// been played. We need to update the last log of a given thread whenever
// we switch threads. If all logs in a given thread are exhausted, we delete
// that thread entry from the last-log-tracker.

void Recover()
{
    Tid2Log::iterator ci_end = last_log_tracker.end();
    for (Tid2Log::iterator ci = last_log_tracker.begin(); ci != ci_end; ++ ci)
        Recover(ci->first);
    fprintf(stderr, "[Atlas] Done undoing %ld log entries\n", replayed_count);
}

void Recover(int tid)
{
    if (done_threads.find(tid) != done_threads.end()) return;
    
    // All replayed logs must have been filtered already.
    Tid2Log::iterator ci = last_log_tracker.find(tid);
    if (ci == last_log_tracker.end()) return;
    
    LogEntry *le = ci->second;

    assert(first_log_tracker.find(tid) != first_log_tracker.end());
    LogEntry *stop_node = first_log_tracker.find(tid)->second;
    
    while (le) {
#ifdef _NVM_TRACE
        fprintf(stderr,
                "Replaying tid = %d le = %p, addr = %p, val = %ld Type = %s\n",
                tid, le, le->Addr, le->ValueOrPtr,
                le->Type == LE_acquire ? "acq" :
                le->Type == LE_release ? "rel" :
                le->Type == LE_str ? "str" :
                le->isMemset() ? "memset" :
                le->isMemcpy() ? "memcpy" :
                le->isMemmove() ? "memmove" :
                le->isStrcpy() ? "strcpy" :
                le->isStrcat() ? "strcat" : "don't-care");
#endif
        // TODO: handle other kinds of locks during recovery.
        if (le->isRelease() || le->isFree()) {
            pair<R2AIter, R2AIter> r2a_iter = map_r2a.equal_range(le);
            if (r2a_iter.first != r2a_iter.second) {
                // We are doing a switch, so adjust the last log
                // from where a subsequent visit should start
                ci->second = GetPrevLogEntry(le);
            }
            for (R2AIter ii = r2a_iter.first; ii != r2a_iter.second; ++ii) {
                LogEntry *new_tid_acq = ii->second.first;
                int new_tid = ii->second.second;

                if (!isAlreadyReplayed(new_tid_acq)) Recover(new_tid);
            }
            if (le->isFree()) {
                Replay(le);
                if (done_threads.find(tid) != done_threads.end()) break;
                MarkReplayed(le);
            }
        }
        else if (le->isAcquire() || le->isAlloc()) {
            if (le->isAlloc()) Replay(le);
            
            if (done_threads.find(tid) != done_threads.end()) break;
            MarkReplayed(le);
        }
        else if (le->isStr() ||
                 le->isMemset() || le->isMemcpy() || le->isMemmove() ||
                 le->isStrcpy() || le->isStrcat())
            Replay(le);

        if (le == stop_node) {
            done_threads.insert(make_pair(tid, true));
            break;
        }
        le = GetPrevLogEntry(le);
    }
}

void MarkReplayed(LogEntry *le)
{
    assert(le->isAcquire() || le->isAlloc() || le->isFree());
    assert(replayed_entries.find(le) == replayed_entries.end());
    replayed_entries[le] = true;
}

bool isAlreadyReplayed(LogEntry *le)
{
    assert(le->isAcquire() || le->isAlloc() || le->isFree());
    return replayed_entries.find(le) != replayed_entries.end();
}

