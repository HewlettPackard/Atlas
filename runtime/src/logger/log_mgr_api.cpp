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

#include "atlas_alloc.h"

// TODO: trylock is not handled. It is unclear how to handle it in the
// general case.

void NVM_Initialize()
{
    assert(!Atlas::LogMgr::hasInstance());
#ifdef _FORCE_FAIL
    //Seed prng for random failing of Atlas
    stand(time(NULL));
#endif
    Atlas::LogMgr::createInstance();
}

void NVM_Finalize()
{
    assert(Atlas::LogMgr::hasInstance());
    Atlas::LogMgr::deleteInstance();
}

void nvm_acquire(void *lock_address)
{
    if (!Atlas::LogMgr::hasInstance()) return;
    Atlas::LogMgr::getInstance().logAcquire(lock_address);
}

void nvm_release(void *lock_address)
{
    if (!Atlas::LogMgr::hasInstance()) return;
    Atlas::LogMgr::getInstance().logRelease(lock_address);
}

void nvm_rwlock_rdlock(void *lock_address)
{
    if (!Atlas::LogMgr::hasInstance()) return;
    Atlas::LogMgr::getInstance().logRdLock(lock_address);
}

void nvm_rwlock_wrlock(void *lock_address)
{
    if (!Atlas::LogMgr::hasInstance()) return;
    Atlas::LogMgr::getInstance().logWrLock(lock_address);
}

void nvm_rwlock_unlock(void *lock_address)
{
    if (!Atlas::LogMgr::hasInstance()) return;
    Atlas::LogMgr::getInstance().logRWUnlock(lock_address);
}

void nvm_begin_durable()
{
    if (!Atlas::LogMgr::hasInstance()) return;
    Atlas::LogMgr::getInstance().logBeginDurable();
}

void nvm_end_durable()
{
    if (!Atlas::LogMgr::hasInstance()) return;
    Atlas::LogMgr::getInstance().logEndDurable();
}

// TODO: bit store support

void nvm_store(void *addr, size_t sz)
{
    if (!Atlas::LogMgr::hasInstance()) return;
    Atlas::LogMgr::getInstance().logStore(addr, sz);
}

void nvm_memset(void *addr, size_t sz)
{
    if (!Atlas::LogMgr::hasInstance()) return;
    Atlas::LogMgr::getInstance().logMemset(addr, sz);
}

void nvm_memcpy(void *dst, size_t sz)
{
    if (!Atlas::LogMgr::hasInstance()) return;
    Atlas::LogMgr::getInstance().logMemcpy(dst, sz);
}

void nvm_memmove(void *dst, size_t sz)
{
    if (!Atlas::LogMgr::hasInstance()) return;
    Atlas::LogMgr::getInstance().logMemmove(dst, sz);
}

size_t nvm_strlen(char *dst)
{
    return strlen(dst)+1;
}

void nvm_strcpy(char *dst, size_t sz)
{
    if (!Atlas::LogMgr::hasInstance()) return;
    Atlas::LogMgr::getInstance().logStrcpy(dst, sz);
}

void nvm_strcat(char *dst, size_t sz)
{
    if (!Atlas::LogMgr::hasInstance()) return;
    Atlas::LogMgr::getInstance().logStrcat(dst, sz);
}

void nvm_log_alloc(void *addr)
{
    if (!Atlas::LogMgr::hasInstance()) return;
    Atlas::LogMgr::getInstance().logAlloc(addr);
}

void nvm_log_free(void *addr)
{
    if (!Atlas::LogMgr::hasInstance()) return;
    Atlas::LogMgr::getInstance().logFree(addr);
}

void nvm_barrier(void *p)
{
    if (!NVM_IsInOpenPR(p, 1)) return;
#if (!defined(DISABLE_FLUSHES) && !defined(_DISABLE_DATA_FLUSH))
    full_fence();
    nvm_clflush((char*)p);
    full_fence();
#endif    
}

// TODO: should this belong to the log manager or the region
// manger. An argument could be made either way. It is part of
// consistency support, so probably belongs to the logger. But if
// someone wants to use the region manager alone, the other option
// could be more attractive.
void nvm_psync(void *start_addr, size_t sz)
{
    assert(Atlas::LogMgr::hasInstance());
    Atlas::LogMgr::getInstance().psync(start_addr, sz);
}

// TODO: The way the LLVM NVM instrumenter is working today, this introduces
// a bug since we are not checking whether start_addr is in the NVM space.
void nvm_psync_acq(void *start_addr, size_t sz)
{
    assert(Atlas::LogMgr::hasInstance());
    Atlas::LogMgr::getInstance().psyncWithAcquireBarrier(start_addr, sz);
}

#if defined(_USE_TABLE_FLUSH)
void AsyncDataFlush(void *p) 
{
    assert(Atlas::LogMgr::hasInstance());
    Atlas::LogMgr::getInstance().asyncDataFlush(p);
}

void AsyncMemOpDataFlush(void *dst, size_t sz)
{
    assert(Atlas::LogMgr::hasInstance());
    Atlas::LogMgr::getInstance().asyncMemOpDataFlush(dst, sz);
}
#endif

#ifdef NVM_STATS
void NVM_PrintStats()
{
    assert(Atlas::LogMgr::hasInstance());
    Atlas::LogMgr::getInstance().printStats();
}
#endif


