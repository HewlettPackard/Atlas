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

namespace Atlas {

void LogMgr::psyncWithAcquireBarrier(void *start_addr, size_t sz)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    if (sz <= 0) return;

    char *last_addr = (char*)start_addr + sz - 1;

    char *cacheline_addr =
        (char*)(((uint64_t)start_addr) & PMallocUtil::get_cache_line_mask());
    char *last_cacheline_addr =
        (char*)(((uint64_t)last_addr) & PMallocUtil::get_cache_line_mask());

    full_fence();
    do {
        NVM_CLFLUSH(cacheline_addr);
        cacheline_addr += PMallocUtil::get_cache_line_size();
    }while (cacheline_addr < last_cacheline_addr+1);
}
    
void LogMgr::psync(void *start_addr, size_t sz)
{
    psyncWithAcquireBarrier(start_addr, sz);
    full_fence();
}

void LogMgr::flushAtEndOfFase()
{
#if defined(_FLUSH_LOCAL_COMMIT) && !defined(DISABLE_FLUSHES)
    assert(TL_FaseFlushPtr_);
    if (!TL_FaseFlushPtr_->empty()) {
        flushCacheLines(*TL_FaseFlushPtr_);
        TL_FaseFlushPtr_->clear();
    }
#elif defined(_USE_TABLE_FLUSH) && !defined(DISABLE_FLUSHES)
    syncDataFlush();
#endif
}
        
} // namespace Atlas
