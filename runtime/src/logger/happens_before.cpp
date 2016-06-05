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
#include "log_structure.hpp"
#include "happens_before.hpp"

namespace Atlas {
    
LastReleaseInfo *LogMgr::findLastReleaseOfLock(void *hash_address)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    LastReleaseInfo *oip = getLastReleaseHeader(hash_address);
    while (oip) {
        ImmutableInfo *ii = oip->Immutable.load(std::memory_order_acquire);
        assert(ii);
        if (ii->IsDeleted) {
          oip = oip->Next;
          continue;
        }
        LogEntry *le = ii->LogAddr;
        assert(le);
        assert(le->isRelease() || le->isRWLockUnlock() || le->isFree());
        if ((le->isFree() && !hash_address) || le->Addr == hash_address)
            return oip;
        oip = oip->Next;
    }
    return nullptr;
}

LastReleaseInfo *LogMgr::findLastReleaseOfLogEntry(LogEntry *candidate_le) 
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    LastReleaseInfo *oip = getLastReleaseHeader(candidate_le->Addr);
    while (oip) {
        ImmutableInfo *ii = oip->Immutable.load(std::memory_order_acquire);
        assert(ii);
        if (ii->IsDeleted) {
          oip = oip->Next;
          continue;
        }
        LogEntry *le = ii->LogAddr;
        assert(le);
        // TODO free not handled?
        assert(le->isRelease() || le->isRWLockUnlock() || le->isFree());
        if (le->Addr != candidate_le->Addr) {
            oip = oip->Next;
            continue;
        }
        else if (le == candidate_le) return oip;
        else return nullptr;
    }
    return nullptr;
}

void LogMgr::addLogToLastReleaseInfo(LogEntry *le,
                                     const MapOfLockInfo& undo_locks)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    assert(le->isRelease() || le->isRWLockUnlock() || le->isFree());

    void *hash_addr = le->isFree() ? NULL : le->Addr;
    bool done = false;
    while (!done) {
        LastReleaseInfo *oi = findLastReleaseOfLock(hash_addr);

        // TODO: Does reusing a deleted entry help in any way?
        if (oi) {
            ImmutableInfo *new_ii =
                createNewImmutableInfo(le, undo_locks, false);
            ImmutableInfo *curr_ii;
            do {
                curr_ii = oi->Immutable.load(std::memory_order_acquire);

                // Even if it was found earlier, it may have been deleted
                // by the helper thread since then. So need to find again.
                oi = findLastReleaseOfLock(hash_addr);
                if (!oi) break;
            }while (!oi->Immutable.compare_exchange_weak(
                        curr_ii, new_ii,
                        std::memory_order_acq_rel, std::memory_order_relaxed));

            // It is ok to delete the old map since any read/write of the
            // map can be done only while holding a lock
            if (oi) {
                assert(!oi->Immutable.load(std::memory_order_relaxed)->IsDeleted);
                done = true;
                delete curr_ii->LockInfoPtr;
            }
        }
        else {
            // TODO
            // This is a bug in a certain way for rwlock. For rwlock, we
            // need to call FindOwnerInfo here again (or within a CAS-loop).
            // Otherwise, two entries for the same lock within the hash table
            // may exist which may or may not create problems.
            LastReleaseInfo *new_entry = createNewLastReleaseInfo(le, undo_locks);
            LastReleaseInfo *first_oip;
            do {
                first_oip = getLastReleaseHeader(hash_addr);
                // Insertion at the head
                new_entry->Next = first_oip;
            }while (!getLastReleaseRoot(hash_addr)->compare_exchange_weak(
                        first_oip, new_entry,
                        std::memory_order_acq_rel, std::memory_order_relaxed));
            done = true;
        }
    }
}

void LogMgr::deleteOwnerInfo(LogEntry *le)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    assert(le->isRelease() || le->isRWLockUnlock() || le->isFree());

    // An owner info may not be found since the one that existed before
    // for this log entry may have been overwritten by one of the user
    // threads.
    LastReleaseInfo *oi = findLastReleaseOfLogEntry(le);
    if (oi) {
        ImmutableInfo *curr_ii = oi->Immutable.load(std::memory_order_acquire);
        ImmutableInfo *new_ii = createNewImmutableInfo(
            curr_ii->LogAddr, *curr_ii->LockInfoPtr, true);
        bool succeeded = oi->Immutable.compare_exchange_weak(
            curr_ii, new_ii, std::memory_order_acq_rel,
            std::memory_order_relaxed);
        if (succeeded) delete curr_ii->LockInfoPtr;
    }
}

ImmutableInfo *LogMgr::createNewImmutableInfo(
    LogEntry *le, const MapOfLockInfo & undo_locks, bool is_deleted)
{
    return new ImmutableInfo(le, new MapOfLockInfo(undo_locks), is_deleted);
}

LastReleaseInfo *LogMgr::createNewLastReleaseInfo(
    LogEntry *le, const MapOfLockInfo & undo_locks)
{
    return new LastReleaseInfo(createNewImmutableInfo(le, undo_locks, false));
}

void LogMgr::setHappensBeforeForAllocFree(LogEntry *le)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    // Happens-after from free -> free and malloc -> free may be set up.
    // For a free (source) -> free (target) relation, the source generation
    // number is already set at this point. But this routine re-sets it to
    // that of the target. We argue that this is not a problem. First, note
    // that the source and target log entries will have different address. If
    // subsequently a malloc (m) log entry leads to another happens-after
    // relation such that m -> free (source) -> free (target) and then
    // the target node is deleted, the HA-link from node m will not be
    // nullified since the target and source log entries will not match.
    LastReleaseInfo *oi = Atlas::LogMgr::getInstance().findLastReleaseOfLock(NULL);
    if (oi)
    {
        ImmutableInfo *ii = oi->Immutable.load(std::memory_order_acquire);
        le->ValueOrPtr = reinterpret_cast<intptr_t>(ii->LogAddr);

        assert(reinterpret_cast<LogEntry*>(le->ValueOrPtr)->isFree());
        le->Size = reinterpret_cast<LogEntry*>(le->ValueOrPtr)->Size;
    }
}

} // namespace Atlas
