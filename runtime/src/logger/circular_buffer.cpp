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
#include <cassert>

#include "log_mgr.hpp"

namespace Atlas {

LogEntry *LogMgr::allocLogEntry()
{
    // note that ctor may not be called
#if defined(_LOG_WITH_MALLOC)
    return (LogEntry *) malloc(sizeof(LogEntry));
#elif defined(_LOG_WITH_NVM_ALLOC)
    return (LogEntry *) Atlas::PRegionMgr::getInstance().allocMemWithoutLogging(
        sizeof(LogEntry), RegionId_);
#else
    LogEntry *le = getNewSlot<LogEntry>(RegionId_, &TL_CbLog_, &CbLogList_);
    assertOneCacheLine(le);
    return le;
#endif    
}

// This function is called when the caller needs a new circular buffer
template<class T>
CbLog<T> *LogMgr::getNewCb(uint32_t size, uint32_t rid, CbLog<T> **log_p,
                           std::atomic<CbListNode<T>*> *cb_list_p)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    // The data structures used in managing the circular buffer need
    // not be in persistent memory. After program termination, the
    // circular buffer metadata is not used. During normal termination,
    // the log file is deleted automatically releasing all the
    // resources for the remaining log entries. In the case of
    // abnormal program termination, the recovery phase never needs
    // these management data structures and the log file is again
    // deleted after successful recovery. The helper thread would free
    // a circular buffer if it is empty but that does not require
    // persistence.

    if (*log_p) (*log_p)->isFilled.store(true, std::memory_order_release);
    
    // Search through the list of cbl_nodes looking for one that is
    // available and is owned by this thread. If found, set isAvailable
    // to false, isfilled to false, and return it.
    CbListNode<T> *curr_search = (*cb_list_p).load(std::memory_order_acquire);
    while (curr_search)
    {
        if (curr_search->isAvailable.load(std::memory_order_acquire) &&
            pthread_equal(curr_search->Tid, pthread_self()))
        {
            assert(curr_search->Cb);
            assert(curr_search->Cb->isFilled.load(std::memory_order_acquire));

            *log_p = curr_search->Cb;
            
            curr_search->Cb->isFilled.store(false, std::memory_order_relaxed);
            curr_search->Cb->Start.store(0, std::memory_order_relaxed);
            curr_search->Cb->End.store(0, std::memory_order_relaxed);

            curr_search->isAvailable.store(false, std::memory_order_release);

            return curr_search->Cb;
        }
        curr_search = curr_search->Next;
    }

    uint32_t is_filled, start_cb, end_cb;
    is_filled = start_cb = end_cb = 0;
    
    CbLog<T> *cb = new CbLog<T>(size+1, is_filled, start_cb, end_cb);
    cb->LogArray = (T*)PRegionMgr::getInstance().allocMemCacheLineAligned(
        cb->Size*sizeof(T), RegionId_, false);

#ifdef NVM_STATS
    Stats_->incrementLogMemUse(cb->Size*sizeof(T));
#endif
    
    *log_p = cb;
    
    // New cbl_node must be inserted at the head
    CbListNode<T> *cbl_node = new CbListNode<T>(
        cb,
        (char*)(cb->LogArray),
        (char*)(cb->LogArray) + cb->Size*sizeof(T) - 1);

    CbListNode<T> *first_p = (*cb_list_p).load(std::memory_order_acquire);
    do {
        cbl_node->Next = first_p;
    }while (!cb_list_p->compare_exchange_weak(
                first_p, cbl_node,
                std::memory_order_acq_rel, std::memory_order_relaxed));

    return cb;
}

// A user thread is the only entity that adds a CB slot
// The helper thread is the only entity that deletes a CB slot
template<class T>
inline T *LogMgr::getNewSlot(uint32_t rid, CbLog<T> **log_p,
                             std::atomic<CbListNode<T>*> *cb_list_p)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    if (!*log_p || (*log_p)->isFull())
        getNewCb<T>(kCircularBufferSize, rid, log_p, cb_list_p);

    ++ TL_LogCounter_;
    if (TL_LogCounter_ % kCircularBufferSize == 0) ++TL_GenNum_;
    
    T *r = &((*log_p)->LogArray[(*log_p)->End.load(
                     std::memory_order_consume)]);
    (*log_p)->End.store(
        (((*log_p)->End).load(std::memory_order_acquire)+1) &
        ((*log_p)->Size-1),
        std::memory_order_release);

    return r;
}

} // namespace Atlas

    
