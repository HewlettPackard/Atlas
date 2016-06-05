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
 

#ifndef LOG_ELISION_HPP
#define LOG_ELISION_HPP

#include <atomic>

namespace Atlas {
    
struct LockReleaseCount
{
    explicit LockReleaseCount(void *addr, uint64_t count)
        : LockAddr{addr},
        Count{count},
        Next{nullptr} {}
    LockReleaseCount() = delete;
    LockReleaseCount(const LockReleaseCount&) = delete;
    LockReleaseCount(LockReleaseCount&&) = delete;
    LockReleaseCount& operator=(const LockReleaseCount&) = delete;
    LockReleaseCount& operator=(LockReleaseCount&&) = delete;
    
    void *LockAddr;
    std::atomic<uint64_t> Count;
    LockReleaseCount *Next;
};

} // namespace Atlas

#endif
