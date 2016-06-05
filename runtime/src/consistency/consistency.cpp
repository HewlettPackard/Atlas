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
 

#include <stdio.h>
#include <stdlib.h>

#include "atlas_api.h"
#include "util.hpp"

#include "pregion_mgr.hpp"
#include "log_mgr.hpp"
#include "consistency_mgr.hpp"
#include "circular_buffer.hpp"

namespace Atlas {
    
bool CSMgr::isFoundInExistingLog(LogEntry *le, uint64_t gen_num) const
{
    Helper::MapLog2Int::const_iterator ci = ExistingRelMap_->find(le);
    if (ci != ExistingRelMap_->end() && ci->second == gen_num) return true;
    return false;
}
    
// Build a Failure Atomic Section (FASection) given the starting log
// entry for the FASE. This data structure is used by the helper
// thread alone. The builder starts with a provided log entry and 
// traverses the thread-specific logs until it either runs out of them
// or comes across the end of an outermost critical section. If the
// former, a FASE is not built. If the latter, a FASE is built and
// returned.   
FASection *CSMgr::buildFASection(LogEntry *le)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    uint32_t lock_count = 0;
    LogEntry *first_le = le;
    while (le) {
        LogEntry *next_le = le->Next.load(std::memory_order_acquire);
        if (!next_le) return nullptr; // always keep one non-null log entry
        
        if (le->isAcquire() || le->isRWLockRdLock() || le->isRWLockWrLock()
            || le->isBeginDurable()) ++lock_count;

        if (le->isRelease() || le->isRWLockUnlock() || le->isEndDurable()) {
            if (lock_count > 0) --lock_count;
            if (!lock_count)
                return new FASection(first_le, le);
        }
        le = next_le;
    }
    assert(0 && "FASE construction in an unexpected code path!");
    return nullptr;
}

void CSMgr::destroyFASections()
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    FaseVec::iterator ci_end = AllFases_.end();
    for (FaseVec::iterator ci = AllFases_.begin(); ci != ci_end; ++ci)
        delete *ci;
}

//
// Examine the pending list, trying to find the log entry that
// immediately happens before it, adding to the durability graph in
// the process. At the end of this function, the pending list is left
// with log entries that cannot be resolved in the current round.
void CSMgr::resolvePendingList()
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    if (PendingList_.empty()) {
        traceHelper("Pending list is empty: graph unchanged\n");
        return;
    }

#if defined(NVM_STATS) && defined(_PROFILE_HT)
    uint64_t start_graph_resolve = atlas_rdtsc();
#endif
    
    // Track the resolved entries using a vector
    typedef std::vector<PendingList::iterator> DelVec;
    DelVec del_vec;
    
    PendingList::iterator ci_end = PendingList_.end();
    for (PendingList::iterator ci = PendingList_.begin(); ci != ci_end; ++ci) {
        LogEntry *le = ci->first;
        
        assert(le);
        assert(le->isAcquire());
        assert(le->ValueOrPtr);

        const DGraph::NodeInfo& node_info = 
            Graph_.getTargetNodeInfo(
                reinterpret_cast<LogEntry*>(le->ValueOrPtr));

        if (node_info.NodeType_ == DGraph::kAvail) {
            Graph_.createEdge(ci->second, node_info.NodeId_);
            // Now that this entry is resolved, tag it for deletion
            del_vec.push_back(ci);
        }
        // Mark the corresponding node unstable only if target is absent.
        else if (node_info.NodeType_ == DGraph::kAbsent)
            if (le->ValueOrPtr)
                set_is_stable(ci->second, false);
    }
    // Actual removal of tagged resolved entries
    DelVec::const_iterator del_ci_end = del_vec.end();
    for (DelVec::const_iterator del_ci = del_vec.begin();
         del_ci != del_ci_end; ++ del_ci)
        PendingList_.erase(*del_ci);

#if defined(NVM_STATS) && defined(_PROFILE_HT)
    uint64_t stop_graph_resolve = atlas_rdtsc();
    Helper::getInstance().incrementTotalGraphResolveTime(
        stop_graph_resolve - start_graph_resolve);
#endif

    traceHelper(get_num_graph_vertices());
    traceHelper(" nodes found in graph after pending list resolution\n");
    traceGraph();
}

// This routine removes nodes (from the durability graph) that cannot
// be resolved 
void CSMgr::removeUnresolvedNodes()
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    if (PendingList_.empty()) {
        traceHelper("Pending list is empty: graph unchanged\n");
        return;
    }

#if defined(NVM_STATS) && defined(_PROFILE_HT)
    uint64_t start_graph_resolve = atlas_rdtsc();
#endif

    MapNodes removed_nodes;
    std::pair<DGraph::VIter, DGraph::VIter> vp;
    for (vp = vertices(Graph_.get_directed_graph());
         vp.first != vp.second; ++ vp.first) {
        DGraph::VDesc nid = *(vp.first);

        // This node has been already processed
        if (removed_nodes.find(nid) != removed_nodes.end()) {
            assert(!is_stable(nid));
            continue;
        }

        if (is_stable(nid)) continue;
        handleUnresolved(nid, &removed_nodes);
    }
    
    // Actually remove the nodes
    MapNodes::const_iterator rm_ci_end = removed_nodes.end();
    MapNodes::const_iterator rm_ci = removed_nodes.begin();
    for (; rm_ci != rm_ci_end; ++ rm_ci)
    {
        Graph_.clear_vertex(rm_ci->first);
        Graph_.remove_vertex(rm_ci->first);
    }

#if defined(NVM_STATS) && defined(_PROFILE_HT)
    uint64_t stop_graph_resolve = atlas_rdtsc();
    Helper::getInstance().incrementTotalGraphResolveTime(
        stop_graph_resolve - start_graph_resolve);
#endif

    traceHelper(get_num_graph_vertices());
    traceHelper(" nodes found in resolved graph\n");
    traceGraph();
}

//
// Given an unstable node, mark other "happen-after" nodes unstable
// as well.
//    
void CSMgr::handleUnresolved(DGraph::VDesc nid, MapNodes *removed_nodes)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    // already handled
    if (removed_nodes->find(nid) != removed_nodes->end()) return;

    (*removed_nodes)[nid] = true;
    
    // if nid is removed, examine all in-edges and for a given in-edge,
    // remove the source node as well
    std::pair<DGraph::IEIter, DGraph::IEIter> iep;
    for (iep = in_edges(nid, Graph_.get_directed_graph());
         iep.first != iep.second; ++iep.first) {
        DGraph::EDesc eid = *iep.first;
        DGraph::VDesc src = source(eid, Graph_.get_directed_graph());

        if (removed_nodes->find(src) != removed_nodes->end()) {
            assert(!is_stable(src));
            continue;
        }
        set_is_stable(src, false);
        handleUnresolved(src, removed_nodes);
    }
}

//
// At this point, a node is in the graph if an only if it belongs to
// the corresponding consistent state. This routine creates a new
// version of the log structure and adds it to the list of such
// outstanding new versions. This new version has new thread-specific
// headers that point to log entries in a way that excludes the FASEs
// corresponding to the graph nodes that belong to this consistent state.
//    
void CSMgr::createVersions(Helper::LogVersions *log_v)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    if (!get_num_graph_vertices()) return;

#if defined(NVM_STATS) && defined(_PROFILE_HT)
    uint64_t start_graph_resolve = atlas_rdtsc();
#endif

    // All nodes in the graph at this point are complete and have been
    // resolved. So all of the corresponding FASEs are marked deletable. 
    std::pair<DGraph::VIter, DGraph::VIter> vp;
    for (vp = vertices(Graph_.get_directed_graph());
         vp.first != vp.second; ++ vp.first) {
        FASection *fase = Graph_.get_fase(*vp.first);
        assert(fase);
        assert(!fase->IsDeleted);
        fase->IsDeleted = true;
    }

    // TODO cache these instances?
    LogStructure *lsp = IsInRecovery_ ?
        LogMgr::getInstance().getRecoveryLogPointer(
            std::memory_order_acquire) :
        LogMgr::getInstance().getLogPointer(std::memory_order_acquire);
    assert(lsp);

    LogStructure *new_header = 0;
    LogStructure *last_ls = 0;
    // We walk the log-structure-header, and for every entry in it,
    // walk through that thread's FASEs and find the first undeleted
    // one.

    Log2Bool deletable_logs; // log entries to be deleted in this version
    while (lsp) {
        FASection *fase = getFirstFase(lsp);
        bool found_undeleted = false;
        if (!fase) {
            found_undeleted = true;
            addLogStructure(lsp->Le, &new_header, &last_ls);
        }
        FASection *last_fase = nullptr;
        while (fase) {
            if (!found_undeleted && !fase->IsDeleted) {
                found_undeleted = true;
                addLogStructure(fase->First, &new_header, &last_ls);
                last_fase = fase;
                fase = fase->Next;
                break;
            }
            else {
                collectLogs(&deletable_logs, fase);
                last_fase = fase;
                fase = fase->Next;
            }
        }
        // For a given thread, we always leave the last log entry around.

        // We may not have found an undeleted FASE if all created
        // FASEs are in the consistent state
        if (!found_undeleted) {
            assert(last_fase);
            assert(last_fase->Last);
            assert(last_fase->Last->Next);
            addLogStructure(last_fase->Last->Next, &new_header, &last_ls);
        }
        lsp = lsp->Next;
    }
    assert(new_header);
    (*log_v).push_back(Helper::LogVer(new_header, deletable_logs));

#if defined(NVM_STATS) && defined(_PROFILE_HT)
    uint64_t stop_graph_resolve = atlas_rdtsc();
    Helper::getInstance().incrementTotalGraphResolveTime(
        stop_graph_resolve - start_graph_resolve);
#endif
}

///
/// @brief Add a new thread specific log header
/// @param le Log entry the new header points to
/// @param header A pointer to a future global header
/// @param last_header The last thread specific log header in sequence
///
/// Create a new thread specific log header. If this is the first in
/// the sequence of thread specific log headers, set the future global
/// header, otherwise have the last thread specific header to point to
/// this newly created one.    
void CSMgr::addLogStructure(LogEntry *le, LogStructure **header,
                            LogStructure **last_header)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    assert(header);
    assert(last_header);
    // TODO ensure that new_ls is getting flushed
    LogStructure *new_ls = LogMgr::getInstance().createLogStructure(le);
    if (!*header) *header = new_ls;
    else {
        assert(*last_header);
        (*last_header)->Next = new_ls;
#if !defined(_DISABLE_LOG_FLUSH) && !defined(DISABLE_FLUSHES)
        NVM_FLUSH_ACQ(&(*last_header)->Next);
#endif                    
    }
    *last_header = new_ls;
}

// Add all log entries of the provided FASE to the map    
void CSMgr::collectLogs(Log2Bool *logs, FASection *fase)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    assert(fase);
    assert(fase->Last);
    assert(fase->IsDeleted);
    
    LogEntry *curr = fase->First;
    do {
        assert(curr);
        (*logs)[curr] = true;
        if (curr == fase->Last) break;
        curr = curr->Next;
    }while (true);
}

} // namespace Atlas
