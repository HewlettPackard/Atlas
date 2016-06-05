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
 

#ifndef STATS_HPP
#define STATS_HPP

#include <cstdlib>
#include <cassert>

#include <stdint.h>
#include <pthread.h>

namespace Atlas {

class Stats {
    static Stats *Instance_;
public:
    
    static Stats& createInstance() {
        assert(!Instance_);
        Instance_ = new Stats();
        return *Instance_;
    }

    static void deleteInstance() {
        assert(Instance_);
        delete Instance_;
        Instance_ = nullptr;
    }

    static Stats& getInstance() {
        assert(Instance_);
        return *Instance_;
    }

    void acquireLock()
        { int status = pthread_mutex_lock(&Lock_); assert(!status); }
    void releaseLock()
        { int status = pthread_mutex_unlock(&Lock_); assert(!status); }

    void incrementCriticalSectionCount()
        { ++TL_CriticalSectionCount; }
    void incrementNestedCriticalSectionCount()
        { ++TL_NestedCriticalSectionCount; }
    void incrementLoggedStoreCount()
        { ++TL_LoggedStoreCount; }
    void incrementCriticalLoggedStoreCount()
        { ++TL_CriticalLoggedStoreCount; }
    void incrementUnloggedStoreCount()
        { ++TL_UnloggedStoreCount; }
    void incrementLogElisionFailCount()
        { ++TL_LogElisionFailCount; }
    void incrementUnloggedCriticalStoreCount()
        { ++TL_UnloggedCriticalStoreCount; }
    void incrementLogMemUse(size_t sz)
        { TL_LogMemUse += sz; }

    void print();
    
private:
    pthread_mutex_t Lock_;
    
    // Computed as the number of lock acquires
    thread_local static uint64_t TL_CriticalSectionCount;

    // Given a lock acquire operation, if there is a lock already held 
    thread_local static uint64_t TL_NestedCriticalSectionCount;

    // Total number of writes logged (memset/memcpy, etc. counted as 1)
    thread_local static uint64_t TL_LoggedStoreCount;

    // Total number of writes encountered within critical sections
    thread_local static uint64_t TL_CriticalLoggedStoreCount;

    // Total number of writes not logged
    thread_local static uint64_t TL_UnloggedStoreCount;

    // Total number of writes for which log elision failed
    thread_local static uint64_t TL_LogElisionFailCount;

    // Total number of writes not logged within critical sections
    thread_local static uint64_t TL_UnloggedCriticalStoreCount;

    // Total memory used by the program log
    thread_local static uint64_t TL_LogMemUse;

    // Total number of CPU cache flushes for logging
    thread_local static uint64_t TL_NumLogFlushes;
};

} // namespace Atlas
    
#endif
