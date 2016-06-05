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

// TODO Cache flush functionality should be in its own class

namespace Atlas {
    
#if (defined(_FLUSH_LOCAL_COMMIT) || defined(_FLUSH_GLOBAL_COMMIT)) &&  \
    !(defined DISABLE_FLUSHES)

void LogMgr::collectCacheLines(SetOfInts *cl_set, void *addr, size_t sz)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    if (!sz) return;
    
    char *last_addr = (char*)addr + sz - 1;
    char *line_addr = (char*)((uint64_t)addr &
                              PMallocUtil::get_cache_line_mask());
    char *last_line_addr = (char*)((uint64_t)last_addr &
                                   PMallocUtil::get_cache_line_mask());
    do {
        (*cl_set).insert((uint64_t)line_addr);
        line_addr += PMallocUtil::get_cache_line_size();
    }while (line_addr < last_line_addr+1);
}

void LogMgr::flushCacheLines(const SetOfInts & cl_set)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    full_fence();
    SetOfInts::const_iterator ci_end = cl_set.end();
    for (SetOfInts::const_iterator ci = cl_set.begin(); ci != ci_end; ++ ci) {
        assert(*ci);
        // We are assuming that a user persistent region is not closed
        // within a critical or atomic section.
#ifdef _FLUSH_GLOBAL_COMMIT
        // This is the only scenario today where the helper thread is
        // flushing data (i.e. essentially writing) data into a user
        // persistent region. But this region may have been closed by
        // the user by this point. So need to check for this
        // situation. This is still not full-proof since the region
        // can be closed between the check and the actual flush.
        // This will at least prevent a fault but more needs to be done
        // to ensure consistency.
        if (!NVM_IsInOpenPR((void*)*ci, 1 /*dummy*/))
            continue;
#endif        
        NVM_CLFLUSH((char*)*ci);
    }
    full_fence();
}

void LogMgr::flushCacheLinesUnconstrained(const SetOfInts & cl_set)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    SetOfInts::const_iterator ci_end = cl_set.end();
    for (SetOfInts::const_iterator ci = cl_set.begin(); ci != ci_end; ++ ci) {
        assert(*ci);
        NVM_CLFLUSH((char*)*ci);
    }
}
#endif

} // namespace Atlas
