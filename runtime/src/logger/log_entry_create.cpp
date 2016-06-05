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
 

#include "log_mgr.hpp"
#include "log_structure.hpp"
#include "happens_before.hpp"

namespace Atlas {

///
/// @brief Given a (non-populated) log entry in persistent memory,
/// populate it using non-temporal stores
/// @param le Pointer to allocated log entry to be populated
/// @param addr Address of memory location or lock
/// @param le_type Type of access to be logged
///
#if defined(_USE_MOVNT)
void LogMgr::logNonTemporal(
    LogEntry *le, void *addr, size_t sz, LogType le_type)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    uintptr_t le_val_or_ptr = 0;
    LogEntry *le_next = nullptr;
    LogEntry le_nt(addr, le_val_or_ptr, le_next, sz, le_type);
    
    if (isStr(le_type))
        memcpy(static_cast<void*>(&le_nt.ValueOrPtr), addr, sz/8);
    else if (isMemop(le_type) || isStrop(le_type)) {
        le_nt.ValueOrPtr =
            reinterpret_cast<uintptr_t>(
                PRegionMgr::getInstance().allocMemWithoutLogging(
                    sz, RegionId_));
        assert(le_nt.ValueOrPtr);
        // TODO mov_nt here
        memcpy((void*)le_nt.ValueOrPtr, addr, sz);
    }
    else assert(isDummy(le_type) || isAlloc(le_type) || isFree(le_type) ||
                isStartSection(le_type) || isEndSection(le_type));
    
    long long unsigned int *from =
        reinterpret_cast<long long unsigned int*>(&le_nt);
    long long unsigned int *to =
        reinterpret_cast<long long unsigned int*>(le);
    uint32_t i;
    assert(sizeof(le_nt) % 8 == 0);
    for (i=0; i < sizeof(le_nt)/8; ++i)
        __builtin_ia32_movntq(
            (__attribute__((__vector_size__(1*sizeof(long long)))) long long*)
            (to+i),
            (__attribute__((__vector_size__(1*sizeof(long long)))) long long)
            *(from+i));
#if !defined(_NO_SFENCE)    
    __builtin_ia32_sfence();
#endif    
}
#endif

///
/// @brief Create a log entry for
/// lock-acquire/lock-release/begin_durable/end_durable  
/// @param le address of lock (NULL for begin/end-durable)
/// @param le_type Type of access to be logged
/// @retval Pointer to created log entry
///    
LogEntry *LogMgr::createSectionLogEntry(void *lock_address, LogType le_type)
{
    assert(le_type == LE_acquire || le_type == LE_release ||
           le_type == LE_begin_durable || le_type == LE_end_durable ||
           le_type == LE_rwlock_rdlock || le_type == LE_rwlock_wrlock ||
           le_type == LE_rwlock_unlock);

    LogEntry *le = allocLogEntry();
    assert(le);

#if defined(_USE_MOVNT)
    logNonTemporal(le, lock_address, 0, le_type);
#else
    // The generation number is currently set for release log entries only.
    uintptr_t le_val_or_ptr = 0 /* initial value */;
    LogEntry *le_next = nullptr; /* initial value */
    new (le) LogEntry(
        lock_address, le_val_or_ptr, le_next,
        isRelease(le_type) ? TL_GenNum_ : sizeof(uintptr_t) /* ignored */,
        le_type);
#endif
    return le;
}

///
/// @brief Create log entry for allocation/deallocation. The address
/// corresponds to that of the isAllocated bit
/// @param addr Address of memory location to be logged
/// @param le_type Type of access to be logged
/// @retval Pointer to created log entry
///    
LogEntry *LogMgr::createAllocationLogEntry(void *addr, LogType le_type)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    assert(le_type == LE_alloc || le_type == LE_free);

    LogEntry *le = allocLogEntry();
    assert(le);

    // The ValueOrPtr field stores the address of the happens-before
    // log entry instead of the last value of the isAllocated bit. The
    // last value is known depending on the type of the log entry.
    // The recovery phase is cognizant of this aspect.
    uintptr_t le_val_or_ptr = 0 /* initial value */;
    LogEntry *le_next = nullptr; /* initial value */
    new (le) LogEntry(addr, le_val_or_ptr, le_next,
                      isFree(le_type) ? TL_GenNum_ : sizeof(size_t),
                      le_type);
    return le;
}

///
/// @brief Create log entry for the store instruction
/// @param addr Address of memory location stored into
/// @param size_in_bits Size (in bits) of the location stored into
/// @retval Pointer to created log entry
///    
LogEntry *LogMgr::createStrLogEntry(void *addr, size_t size_in_bits)
{
    assert(size_in_bits <= 8*sizeof(uintptr_t));
    assert(!(size_in_bits % 8));

    LogEntry *le = allocLogEntry();
    assert(le);

#if defined(_USE_MOVNT)
    logNonTemporal(le, addr, size_in_bits, LE_str);
#else
    uintptr_t le_val_or_ptr = 0 /* initial value */;
    LogEntry *le_next = nullptr; /* initial value */
    new (le) LogEntry(addr, le_val_or_ptr, le_next, size_in_bits, LE_str);
    memcpy(reinterpret_cast<void*>(&le->ValueOrPtr), addr, size_in_bits/8);
#endif
    return le;
}

///
/// @brief Create log entry for memop/strop instructions
/// @param addr Address of target memory location
/// @param sz Size of operation
/// @param le_type Type of operation
/// @retval Pointer to created log entry
///    
LogEntry *LogMgr::createMemStrLogEntry(void *addr, size_t sz, LogType le_type)
{
    assert(le_type == LE_memset || le_type == LE_memcpy ||
           le_type == LE_memmove ||
           le_type == LE_strcpy || le_type == LE_strcat);

    LogEntry *le = allocLogEntry();
    assert(le);

#if defined(_USE_MOVNT)
    logNonTemporal(le, addr, sz, le_type);
#else
    uintptr_t le_val_or_ptr = 0 /* ignored */;
    LogEntry *le_next = nullptr; /* initial value */
    new (le) LogEntry(addr, le_val_or_ptr, le_next, sz, le_type);

    // For a memop/strop, ValueOrPtr is a pointer to the sequence of
    // old values
#if defined(_LOG_WITH_MALLOC)    
    le->ValueOrPtr = (intptr_t)(char *) malloc(sz);
#else
//#elif defined(_LOG_WITH_NVM_ALLOC)    // no special casing required
    le->ValueOrPtr =
        reinterpret_cast<intptr_t>(
            PRegionMgr::getInstance().allocMemWithoutLogging(sz, RegionId_));
#endif
    assert(le->ValueOrPtr);
    memcpy(reinterpret_cast<void*>(le->ValueOrPtr), addr, sz);
    NVM_PSYNC(reinterpret_cast<void*>(le->ValueOrPtr), sz);
#endif    
    return le;
}

///
/// @brief Create a dummy log entry
/// @retval Pointer to created log entry
///    
LogEntry *LogMgr::createDummyLogEntry()
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    LogEntry *le = allocLogEntry();
    assert(le);

#if defined(_USE_MOVNT)
    logNonTemporal(le, 0, 0, LE_dummy);
#else    
    memset(le, 0, sizeof(LogEntry));
    le->Type = LE_dummy;
#endif    
    return le;
}

///
/// @brief Create a thread specific log header
/// @param le First log entry for this thread
/// @retval Pointer to the log header created
///    
/// The following is called by the helper thread alone (today).
///    
LogStructure *LogMgr::createLogStructure(LogEntry *le)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    LogStructure *lsp = static_cast<LogStructure*>(
        PRegionMgr::getInstance().allocMemWithoutLogging(
            sizeof(LogStructure), RegionId_));
    assert(lsp);

    new (lsp) LogStructure(le, nullptr);

    // Because of 16-byte alignment of all allocated memory on NVRAM, the above
    // two fields will always be on the same cache line
    flushLogUncond(lsp);
    return lsp;
}

} // namespace Atlas
