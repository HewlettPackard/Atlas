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
#include <string>

#include <pthread.h>

#include "atlas_api.h"

#include "stats.hpp"

// TODO
__thread uint64_t num_flushes = 0;
    
namespace Atlas {

Stats *Stats::Instance_{nullptr};

thread_local uint64_t Stats::TL_CriticalSectionCount{0};
thread_local uint64_t Stats::TL_NestedCriticalSectionCount{0};
thread_local uint64_t Stats::TL_LoggedStoreCount{0};
thread_local uint64_t Stats::TL_CriticalLoggedStoreCount{0};
thread_local uint64_t Stats::TL_UnloggedStoreCount{0};
thread_local uint64_t Stats::TL_UnloggedCriticalStoreCount{0};
thread_local uint64_t Stats::TL_LogElisionFailCount{0};
thread_local uint64_t Stats::TL_LogMemUse{0};
thread_local uint64_t Stats::TL_NumLogFlushes{0};

// acquire this lock when printing something to stderr
void Stats::print()
{
    acquireLock();
    
    std::cout << "[Atlas-stats] Begin thread " << pthread_self() << std::endl;

    std::cout << "\t# critical sections: " <<
        TL_CriticalSectionCount << std::endl;
    std::cout << "\t# nested critical sections: " <<
        TL_NestedCriticalSectionCount << std::endl;
    std::cout << "\t# logged stores: " <<
        TL_LoggedStoreCount << std::endl;
    std::cout << "\t# logged stores in critical sections: " <<
        TL_CriticalLoggedStoreCount << std::endl;
    std::cout << "\t# unlogged stores: " <<
        TL_UnloggedStoreCount << std::endl;
    std::cout << "\t# unlogged stores in critical sections: " <<
        TL_UnloggedCriticalStoreCount << std::endl;
    std::cout << "\t# log elision failures (outside critical sections): " <<
        TL_LogElisionFailCount << std::endl;
    std::cout << "\tLog memory usage: " << TL_LogMemUse << std::endl;
    std::cout << "\t# Log entries (total): " <<
        TL_CriticalSectionCount * 2 + TL_LoggedStoreCount << std::endl;
    std::cout << "\t# flushes: " << num_flushes << std::endl;

    std::cout << "[Atlas-stats] End thread " << pthread_self() << std::endl;

    releaseLock();
}

} // namespace Atlas

