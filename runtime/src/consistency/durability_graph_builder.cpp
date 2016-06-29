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
 

#include "consistency_configs.hpp"
#include "consistency_mgr.hpp"
#include "durability_graph.hpp"
#include "helper.hpp"

namespace Atlas {

static inline void addThreadNode(
    CSMgr::MapNodes *thread_nodes, DGraph::VDesc nid)
{
    assert(thread_nodes->find(nid) == thread_nodes->end());
    thread_nodes->insert(std::make_pair(nid, true));
}

static inline bool hasThreadNode(
    const CSMgr::MapNodes& thread_nodes, DGraph::VDesc nid) 
{
    return thread_nodes.find(nid) != thread_nodes.end();
}
            
void CSMgr::buildInitialGraph(LogStructure *lsp)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
#if defined(NVM_STATS) && defined(_PROFILE_HT)
    uint64_t start_graph_build = atlas_rdtsc();
#endif
    
    // This loop goes through the log entries of one thread at a time
    while (lsp) {
        LogEntry *current_le = lsp->Le;
        assert(current_le);
        
        DGraph::VDesc prev_nid = 0;
        FASection *prev_fase = nullptr;
        bool is_first_node = true;
        uint32_t fase_count = 0;
        MapNodes thread_nodes;
        // This loop goes through the FASEs
        while (true) {
            ++fase_count;

            // TODO: If a consistent state is not found, the number
            // of FASEs examined should be increased.

            // There is a configurable maximum number of FASEs chosen
            // from a given thread in a given analysis step
            if (fase_count > kFaseAnalysisLimit) break;
                
            if (areUserThreadsDone()) {
                IsParentDone_ = true;
                break;
            }

            // Build a FASE starting with this log entry
            FASection *current_fase = buildFASection(current_le);
            if (!current_fase) break; // this thread is done
            else {
                if (!isFirstFaseFound(lsp))
                    addFirstFase(lsp, current_fase);
                addFaseToVec(current_fase);
            }

            if (prev_fase) prev_fase->Next = current_fase;
                
            DGraph::VDesc nid = Graph_.createNode(current_fase);
            addThreadNode(&thread_nodes, nid);
            
            if (!is_first_node) {
                assert(prev_nid);
                Graph_.createEdge(nid, prev_nid);
            }

            is_first_node = false;
            prev_nid = nid;

            // This loop goes through the log entries of a FASE
            do {
                addSyncEdges(thread_nodes, current_le, nid);

                if (current_le == current_fase->Last) break;

                // No need for an atomic read, we are guaranteed
                // at this point that the next ptr won't change.
                current_le = current_le->Next;
                
            }while (true);

            prev_fase = current_fase;
            current_le = current_le->Next.load(std::memory_order_acquire);
        }
        if (IsParentDone_) break;
        lsp = lsp->Next;
    }

#if defined(NVM_STATS) && defined(_PROFILE_HT)
    uint64_t stop_graph_build = atlas_rdtsc();
    Helper::getInstance().incrementTotalGraphBuildTime(
        stop_graph_build - start_graph_build);
#endif

    traceHelper(get_num_graph_vertices());
    traceHelper(" nodes found in initial graph\n");
    traceGraph();
}

// TODO Take care of reentrant locking.
// Keeping a thread-local map to filter out locks held at a certain point
// of time can help here.

///
/// @brief Add synchronizes-with edges between log entries
/// @param thread_nodes Nodes created till now by this thread
/// @param le Log entry to be processed
/// @param nid Node id of the FASE containing le    
///
/// An acquire log entry is examined to see whether a
/// synchronizes-with relation should be created. A number of
/// scenarios can arise:
/// (1) The acquire log entry does not synchronize-with anything.
/// (2) This is an online analysis phase and the acquire log entry "le"
/// synchronizes-with a release log entry "rle" but "rle" was deleted
/// earlier while computing a consistent state. In such a case, no
/// synchronizes-with relation needs to be added.
/// (3) If this is invoked during recovery and #2 does not hold and
/// the acquire log entry "le" synchronizes-with a release log entry
/// "rle" but "rle" is not found in the log entries left over at the
/// start of recovery, no synchronizes-with relation needs to be
/// added.
/// (4) If the above are not satisfied and the acquire log entry "le"
/// synchronizes-with a release log entry "rle" and "rle" belongs to an
/// existing node "tgt" of the graph, add a synchronizes-with edge
/// from nid to tgt.
/// (5) If the acquire log entry "le" synchronizes with a release log
/// entry "rle" and "rle" does not belong to an existing node of the
/// graph, "le" is added to a pending list to be examined again after
/// all the nodes of the graph are created. If it cannot be resolved
/// even then, this FASE cannot belong to the consistent state under
/// creation.
/// If "le" is of release type, track it for future synchronizes-with
/// relationship creation.
///    
// TODO: do other log types need handling? free and rw-type    
void CSMgr::addSyncEdges(
    const MapNodes& thread_nodes, LogEntry *le, DGraph::VDesc nid)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    // First filter out the scenario where the target is already deleted
    if (le->isAcquire() && le->ValueOrPtr) {
        LogEntry *rel_le = (LogEntry*)(le->ValueOrPtr);

        // Note: the following call deletes the found entry.
        // ValueOrPtr is currently not of atomic type. This
        // is still ok as long as there is a single helper
        // thread. 
        if (Helper::getInstance().isDeletedByHelperThread(rel_le, le->Size))
            le->ValueOrPtr = 0;
        else if (IsInRecovery_ && !isFoundInExistingLog(rel_le, le->Size))
            le->ValueOrPtr = 0;
    }
                        
    if (le->isAcquire() && le->ValueOrPtr) {
        const DGraph::NodeInfo& node_info = 
            Graph_.getTargetNodeInfo((LogEntry *)le->ValueOrPtr);
        if (node_info.NodeType_ == DGraph::kAvail) {
            if (!hasThreadNode(thread_nodes, node_info.NodeId_))
                Graph_.createEdge(nid, node_info.NodeId_);
        }
        else if (node_info.NodeType_ == DGraph::kAbsent)
            addToPendingList(le, nid);
    }
    else if (le->isRelease())
        Graph_.addToNodeInfoMap(le, nid, DGraph::kAvail);
}
    
DGraph::NodeInfo DGraph::getTargetNodeInfo(LogEntry *tgt_le)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    NodeInfoMap::const_iterator ci = NodeInfoMap_.find(tgt_le);
    if (ci == NodeInfoMap_.end())
        return NodeInfo(static_cast<VDesc>(0) /* dummy */, kAbsent);
    else return ci->second;
}

void DGraph::addToNodeInfoMap(LogEntry *le, VDesc nid, NodeType node_type)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    assert(node_type == kAvail); // currently 
    NodeInfoMap_.insert(std::make_pair(le, NodeInfo(nid, node_type)));
}

void DGraph::trace()
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    std::pair<VIter,VIter> vp;
    for (vp = vertices(DirectedGraph_); vp.first != vp.second; ++ vp.first)
    {
        VDesc nid = *vp.first;
        FASection *fase = DirectedGraph_[nid].Fase_;

        traceHelper("======================\n");
        traceHelper("\tNode id: ");
        traceHelper(nid);
        traceHelper("\tFASE: ");
        traceHelper(fase);
        traceHelper(" isStable: ");
        traceHelper(DirectedGraph_[nid].isStable_);

        traceHelper("\n\tHere are the log records:\n");
        PrintLogs(fase);

        std::pair<IEIter, IEIter> iep;
        int count = 0;
        traceHelper("\n\tHere are the sources:\n");
        for (iep = in_edges(nid, DirectedGraph_);
             iep.first != iep.second; ++ iep.first)
        {
            EDesc eid = *iep.first;
            VDesc src = source(eid, DirectedGraph_);
            FASection *src_fase = DirectedGraph_[src].Fase_;
            ++count;
            traceHelper("\t\tNode id: ");
            traceHelper(src);
            traceHelper(" FASE: ");
            traceHelper(src_fase);
        }
        traceHelper("\n\t# incoming edges: ");
        traceHelper(count);
        traceHelper('\n');
    }
}

void DGraph::PrintLogs(FASection *fase)
{
    LogEntry *current_le = fase->First;
    assert(current_le);

    LogEntry *last_le = fase->Last;
    assert(last_le);

    do
    {
        switch(current_le->Type) 
        {
            case LE_dummy:
                PrintDummyLog(current_le);
                break;
            case LE_acquire:
                PrintAcqLog(current_le);
                break;
            case LE_rwlock_rdlock:
                PrintRdLockLog(current_le);
                break;
            case LE_rwlock_wrlock:
                PrintWrLockLog(current_le);
                break;
            case LE_begin_durable:
                PrintBeginDurableLog(current_le);
                break;
            case LE_release:
                PrintRelLog(current_le);
                break;
            case LE_rwlock_unlock:
                PrintRWUnlockLog(current_le);
                break;
            case LE_end_durable:
                PrintEndDurableLog(current_le);
                break;
            case LE_str:
                PrintStrLog(current_le);
                break;
            case LE_memset:
            case LE_memcpy:
            case LE_memmove:
                PrintMemOpLog(current_le);
                break;
            case LE_strcpy:
            case LE_strcat:
                PrintStrOpLog(current_le);
                break;
            case LE_alloc:
                PrintAllocLog(current_le);
                break;
            case LE_free:
                PrintFreeLog(current_le);
                break;
            default:
                assert(0);
        }
        if (current_le == last_le) break;
        current_le = current_le->Next;
    }while (true);
}

// TODO The following should call a LogEntry interface with the
// appropriate stream     
void DGraph::PrintDummyLog(LogEntry *le)
{
    traceHelper("\t\tle = ");
    traceHelper(le);
    traceHelper(" type dummy\n");
}

void DGraph::PrintAcqLog(LogEntry *le)
{
    traceHelper("\t\tle = ");
    traceHelper(le);
    traceHelper(" lock = ");
    traceHelper(le->Addr);
    traceHelper(" ha = ");
    traceHelper((intptr_t*)(le->ValueOrPtr));
    traceHelper(" type = acq next = ");
    traceHelper(le->Next.load(std::memory_order_relaxed));
}

// TODO complete
void DGraph::PrintRdLockLog(LogEntry *le)
{
    traceHelper("\t\tle = ");
    traceHelper(le);
    traceHelper(" lock = ");
    traceHelper(le->Addr);
    traceHelper(" ha = ");
    traceHelper((intptr_t*)(le->ValueOrPtr));
    traceHelper(" type = rw_rd next = ");
    traceHelper(le->Next.load(std::memory_order_relaxed));
}

// TODO complete
void DGraph::PrintWrLockLog(LogEntry *le)
{
    traceHelper("\t\tle = ");
    traceHelper(le);
    traceHelper(" lock = ");
    traceHelper(le->Addr);
    traceHelper(" ha = ");
    traceHelper((intptr_t*)(le->ValueOrPtr));
    traceHelper(" type = rw_wr next = ");
    traceHelper(le->Next.load(std::memory_order_relaxed));
}

void DGraph::PrintBeginDurableLog(LogEntry *le)
{
    traceHelper("\t\tle = ");
    traceHelper(le);
    traceHelper(" type = begin_durable next = ");
    traceHelper(le->Next.load(std::memory_order_relaxed));
}

void DGraph::PrintRelLog(LogEntry *le)
{
    assert(!le->ValueOrPtr);
    traceHelper("\t\tle = ");
    traceHelper(le);
    traceHelper(" lock = ");
    traceHelper(le->Addr);
    traceHelper(" type = rel next = ");
    traceHelper(le->Next.load(std::memory_order_relaxed));
}

// TODO complete
void DGraph::PrintRWUnlockLog(LogEntry *le)
{
    assert(!le->ValueOrPtr);
    traceHelper("\t\tle = ");
    traceHelper(le);
    traceHelper(" lock = ");
    traceHelper(le->Addr);
    traceHelper(" type = rw_unlock next = ");
    traceHelper(le->Next.load(std::memory_order_relaxed));
}

void DGraph::PrintEndDurableLog(LogEntry *le)
{
    traceHelper("\t\tle = ");
    traceHelper(le);
    traceHelper(" type = end_durable next = ");
    traceHelper(le->Next.load(std::memory_order_relaxed));
}

void DGraph::PrintStrLog(LogEntry *le)
{
    traceHelper("\t\tle = ");
    traceHelper(le);
    traceHelper(" addr = ");
    traceHelper(le->Addr);
    traceHelper(" val = ");
    traceHelper(le->ValueOrPtr);
    traceHelper(" size = ");
    traceHelper(le->Size);
    traceHelper(" type = str next = ");
    traceHelper(le->Next.load(std::memory_order_relaxed));
}

// TODO complete
void DGraph::PrintMemOpLog(LogEntry *le)
{
    traceHelper("\t\tle = ");
    traceHelper(le);
    traceHelper(" addr = ");
    traceHelper(le->Addr);
    traceHelper(" size = ");
    traceHelper(le->Size);
    traceHelper(" type = memop next = ");
    traceHelper(le->Next.load(std::memory_order_relaxed));
}

// TODO complete
void DGraph::PrintStrOpLog(LogEntry *le)
{
    traceHelper("\t\tle = ");
    traceHelper(le);
    traceHelper(" addr = ");
    traceHelper(le->Addr);
    traceHelper(" size = ");
    traceHelper(le->Size);
    traceHelper(" type = strop next = ");
    traceHelper(le->Next.load(std::memory_order_relaxed));
}

void DGraph::PrintAllocLog(LogEntry *le)
{
    traceHelper("\t\tle = ");
    traceHelper(le);
    traceHelper(" addr = ");
    traceHelper(le->Addr);
    traceHelper(" val = ");
    traceHelper((void*)le->ValueOrPtr);
    traceHelper(" size = ");
    traceHelper(le->Size);
    traceHelper(" type = alloc next = ");
    traceHelper(le->Next.load(std::memory_order_relaxed));
}

void DGraph::PrintFreeLog(LogEntry *le)
{
    traceHelper("\t\tle = ");
    traceHelper(le);
    traceHelper(" addr = ");
    traceHelper(le->Addr);
    traceHelper(" val = ");
    traceHelper((void*)le->ValueOrPtr);
    traceHelper(" size = ");
    traceHelper(le->Size);
    traceHelper(" type = free next = ");
    traceHelper(le->Next.load(std::memory_order_relaxed));
}
        
} // namespace Atlas
