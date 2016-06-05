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
 

#ifndef LOG_CONFIGS_HPP
#define LOG_CONFIGS_HPP

#include <stdint.h>

namespace Atlas {

const uint64_t kHashTableSize = 1 << 10;
const uint64_t kHashTableMask = kHashTableSize - 1;
const uint32_t kShift = 3;
const uint32_t kWorkThreshold = 100;
const uint32_t kCircularBufferSize = 1024 * 16 - 1;
    
// At limit for using 4 bits
// Combined strncat and strcat, strcpy and strncpy
enum LogType {
    LE_dummy, LE_acquire, LE_rwlock_rdlock, LE_rwlock_wrlock,
    LE_begin_durable, LE_release, LE_rwlock_unlock, LE_end_durable,
    LE_str, LE_memset, LE_memcpy, LE_memmove,
    LE_strcpy, LE_strcat, LE_alloc, LE_free
};

} // namespace Atlas

#endif
