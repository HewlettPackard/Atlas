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
 

#ifndef HELPER_HPP
#define HELPER_HPP

#include <cstdio>
#include <fstream>
#include <atomic>
#include <cassert>
#include <map>
#include <vector>
#include <string>

#include "log_mgr.hpp"

namespace Atlas {

typedef std::map<LogEntry*, bool> Log2Bool;

// The top-level instance of the helper thread    
class Helper {
    static Helper *Instance_;

public:

    struct LogVer {
        explicit LogVer(LogStructure *ls, Log2Bool del_logs) 
            : LS_(ls), Del_(del_logs) {}
        LogVer() = delete;
        
        LogStructure *LS_;
        Log2Bool Del_;
    };

    typedef std::vector<LogVer> LogVersions;
    typedef std::map<LogEntry*, uint64_t> MapLog2Int;
    typedef std::multimap<LogEntry*, uint64_t> MultiMapLog2Int;
    typedef MultiMapLog2Int::iterator DelIter;

    static Helper& createInstance() {
        assert(!Instance_);
        Instance_ = new Helper();
        return *Instance_;
    }

    static void deleteInstance() {
        assert(Instance_);
        delete Instance_;
        Instance_ = nullptr;
    }

    static Helper& getInstance() {
        assert(Instance_);
        return *Instance_;
    }

    uint64_t get_iter_num() const { return IterNum_; }

    void doConsistentUpdate(void*);
    void addEntryToDeletedMap(LogEntry *le, uint64_t gen_num)
        { DeletedRelLogs_.insert(std::make_pair(le, gen_num)); }
    
    bool isDeletedByHelperThread(LogEntry *le, uint64_t gen_num);
    template<class T> void trace(T s)
        { TraceStream_ << s; }

    void incrementTotalGraphBuildTime(uint64_t inc)
        { TotalGraphBuildTime_ += inc; }
    void incrementTotalGraphResolveTime(uint64_t inc)
        { TotalGraphResolveTime_ += inc; }
    void incrementTotalPruneTime(uint64_t inc)
        { TotalPruneTime_ += inc; }

    void printStats();
    
private:
    
    Helper() :
        IterNum_{0}, 
        IsInRecovery_{false},
        LogVersions_{},
        DeletedRelLogs_{},
        ExistingRelMap_{},
#if defined(_NVM_TRACE) || defined(_NVM_VERBOSE_TRACE)
        TraceStream_{"/tmp/atlas_log_pruner.txt"},
#endif                          
        TotalGraphBuildTime_{0},
        TotalGraphResolveTime_{0},
        TotalPruneTime_{0}
        {
#if defined(_NVM_TRACE) || defined(_NVM_VERBOSE_TRACE)
            assert(TraceStream_ && "Error opening trace file");
#endif            
        }

    ~Helper() {
#if defined(_NVM_TRACE) || defined(_NVM_VERBOSE_TRACE)
        TraceStream_.close();
#endif
        
    }
    
    uint64_t IterNum_;
    bool IsInRecovery_;
    LogVersions LogVersions_;
    MultiMapLog2Int DeletedRelLogs_;
    MapLog2Int ExistingRelMap_;
    std::ofstream TraceStream_;

    // there is only 1 helper thread today
    uint64_t TotalGraphBuildTime_;
    uint64_t TotalGraphResolveTime_;
    uint64_t TotalPruneTime_;
    
    void collectRelLogEntries(LogStructure *lsp);
    bool areUserThreadsDone() const
        { return LogMgr::getInstance().areUserThreadsDone(); }
};
    
} // namespace Atlas

#endif
