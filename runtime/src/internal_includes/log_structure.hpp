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
 

#ifndef LOG_STRUCTURE_HPP
#define LOG_STRUCTURE_HPP

#include <atomic>

#include <stdlib.h>
#include <stdint.h>

#include "log_configs.hpp"

namespace Atlas {

// Structure of an undo log entry
struct LogEntry
{
    LogEntry(void *addr, uintptr_t val_or_ptr, LogEntry *next,
             size_t sz, LogType type) 
        : Addr{addr}, ValueOrPtr{val_or_ptr}, Next{next},
        Size{sz}, Type{type} {}

    void *Addr; /* address of mloc or lock object */
    uintptr_t ValueOrPtr; /* either value or ptr (for sync ops) */
    std::atomic<LogEntry*> Next; /* ptr to next log entry in program order */
    size_t Size:60; /* mloc size or a generation # for sync ops */
    LogType Type:4;

    bool isDummy() const { return Type == LE_dummy; }
    bool isAcquire() const { return Type == LE_acquire; }
    bool isRWLockRdLock() const { return Type == LE_rwlock_rdlock; }
    bool isRWLockWrLock() const { return Type == LE_rwlock_wrlock; }
    bool isBeginDurable() const { return Type == LE_begin_durable; }
    bool isRelease() const { return Type == LE_release; }
    bool isRWLockUnlock() const { return Type == LE_rwlock_unlock; }
    bool isEndDurable() const { return Type == LE_end_durable; }
    bool isStr() const { return Type == LE_str; }
    bool isMemset() const { return Type == LE_memset; }
    bool isMemcpy() const { return Type == LE_memcpy; }
    bool isMemmove() const { return Type == LE_memmove; }
    bool isMemop() const {
        return Type == LE_memset || Type == LE_memcpy || Type == LE_memmove;
    }
    bool isAlloc() const { return Type == LE_alloc; }
    bool isFree() const { return Type == LE_free; }
    bool isStrcpy() const { return Type == LE_strcpy; }
    bool isStrcat() const { return Type == LE_strcat; }
    bool isStrop() const { return Type == LE_strcpy || Type == LE_strcat; }
    bool isStartSection() const {
    return isAcquire() || isRWLockRdLock() || isRWLockWrLock() ||
        isBeginDurable();
    }
    bool isEndSection() const 
        { return isRelease() || isRWLockUnlock() || isEndDurable(); }
};

#define LAST_LOG_ELEM(p) ((char*)(p)+24)

// Log structure header: A shared statically allocated header points at
// the start of this structure, so it is available to all threads. Every
// entry has a pointer to a particular thread's log structure and a next
// pointer
struct LogStructure
{
    LogStructure(LogEntry *le, LogStructure *next) 
        : Le{le},
        Next{next} {}
    
    LogEntry *Le; // points to first non-deleted thread-specific log entry 
    // Insertion happens at the head, so the following is write-once before
    // getting published.
    LogStructure *Next;
};

static inline bool isDummy(LogType le_type)
{
    return le_type == LE_dummy;
}
    
static inline bool isAcquire(LogType le_type)
{
    return le_type == LE_acquire;
}

static inline bool isRWLockRdLock(LogType le_type)
{
    return le_type == LE_rwlock_rdlock;
}

static inline bool isRWLockWrLock(LogType le_type)
{
    return le_type == LE_rwlock_wrlock;
}

static inline bool isBeginDurable(LogType le_type)
{
    return le_type == LE_begin_durable;
}

static inline bool isRelease(LogType le_type)
{
    return le_type == LE_release;
}

static inline bool isRWLockUnlock(LogType le_type)
{
    return le_type == LE_rwlock_unlock;
}

static inline bool isEndDurable(LogType le_type)
{
    return le_type == LE_end_durable;
}

static inline bool isStr(LogType le_type)
{
    return le_type == LE_str;
}

static inline bool isMemset(LogType le_type)
{
    return le_type == LE_memset;
}

static inline bool isMemcpy(LogType le_type)
{
    return le_type == LE_memcpy;
}

static inline bool isMemmove(LogType le_type)
{
    return le_type == LE_memmove;
}

static inline bool isMemop(LogType le_type)
{
    return le_type == LE_memset || le_type == LE_memcpy ||
        le_type == LE_memmove;
}

static inline bool isAlloc(LogType le_type)
{
    return le_type == LE_alloc;
}

static inline bool isFree(LogType le_type)
{
    return le_type == LE_free;
}
    
static inline bool isStrcpy(LogType le_type)
{
    return le_type == LE_strcpy;
}

static inline bool isStrcat(LogType le_type)
{
    return le_type == LE_strcat;
}

static inline bool isStrop(LogType le_type)
{
    return le_type == LE_strcpy || le_type == LE_strcat;
}

static inline bool isStartSection(LogType le_type)
{
    return isAcquire(le_type) || isRWLockRdLock(le_type) ||
        isRWLockWrLock(le_type) || isBeginDurable(le_type);
}

static inline bool isEndSection(LogType le_type)
{
    return isRelease(le_type) || isRWLockUnlock(le_type) ||
        isEndDurable(le_type);
}

} // namespace Atlas

#endif
