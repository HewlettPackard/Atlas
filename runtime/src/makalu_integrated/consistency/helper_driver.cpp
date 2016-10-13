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
#include <sys/time.h>

#include "atlas_api.h"
#include "helper.h"

#define HELPER_OCS_ANALYSIS_LIMIT 8

#if defined(_FLUSH_GLOBAL_COMMIT) && !defined(DISABLE_FLUSHES) && \
    !defined(_DISABLE_DATA_FLUSH)
SetOfInts *global_flush_ptr = 0;
#endif

bool dump_initial_graph = false;
bool dump_resolved_graph = false;
bool dump_versions = false;
bool dump_dtor = false;

uint64_t removed_log_count = 0;

FILE *helper_trace_file = NULL;

//Is_Recovery = false;
__thread bool Is_Recovery = false;

LogStructure *recovery_time_lsp = NULL;

void *helper(void *arg_lsp)
{
#ifdef _DISABLE_HELPER
    return 0;
#endif

#if defined(_NVM_TRACE) || defined(_NVM_VERBOSE_TRACE)
    helper_trace_file = fopen("htf.txt", "w");
    assert(helper_trace_file);
#endif
    
#if defined(_FLUSH_GLOBAL_COMMIT) && !defined(DISABLE_FLUSHES) && \
    !defined(_DISABLE_DATA_FLUSH)
    global_flush_ptr = new SetOfInts;
#endif
    
// TODO Have the helper thread work in _small_ chunks. Given that an
// iteration of the helper thread can be quite expensive, we want to
// make sure that the helper thread can be joined as soon as the user
// thread is done. An alternative is for the user thread to send a signal
// to the helper thread.

    int iter_num = 0;
    bool is_parent_done = false;
    LogVersions log_v;
    OcsVec ocs_vec;

    // Any given log entry address can appear at most once since these
    // are picked up from the existing log entries.
    MapLog2Int existing_rel_map;
    
#ifdef _PROFILE_HT
    uint64_t start_graph_build, end_graph_build, total_graph_build = 0;
    uint64_t start_graph_resolve, end_graph_resolve, total_graph_resolve = 0;
    uint64_t start_graph_prune, end_graph_prune, total_graph_prune = 0;
#endif

    //hinting GC that we will never call malloc from this thread of execution
    GC_declare_never_allocate(1);
    // During normal execution, the argument is NULL. 
    if (arg_lsp)
    {
        fprintf(stderr,
                "[Atlas] Advancing consistent state before undoing ...\n");
        CollectRelLogEntries((LogStructure*)arg_lsp, &existing_rel_map);
    }
    
    do
    {
        ++iter_num;

        LogStructure *lsp = 0;

        // TODO May need a more elaborate producer-consumer implementation
        // but the current solution appears to be reasonable. A motivation
        // of the current solution is to avoid locking within the critical
        // path of the user threads (locking happens only within
        // NVM_Finalize but not within the regular execution paths). But
        // this also means that there may be some lost signals
        // allowing the helper thread to sleep when there is in fact
        // some work to do. But the helper thread will eventually be
        // woken up and the signal sent out in the finalization step
        // will never be lost.
        if (!arg_lsp)
        {
            pthread_mutex_lock(&helper_lock);
            // We don't want to check this condition within a loop since we
            // need to do some work. If a spurious wakeup happens and that
            // should be rare, the helper thread may go through a round of
            // analysis that may or may not result in wasted work. But that
            // should not be a problem in the general case.
            if (!ALAR(all_done)) pthread_cond_wait(&helper_cond, &helper_lock);
            pthread_mutex_unlock(&helper_lock);
            
            if (ALAR(all_done)) break;

            lsp = (LogStructure*) ALAR(*log_structure_header_p);
        }
        else if (!recovery_time_lsp)
        {
            Is_Recovery =  true;
            lsp = (LogStructure*)arg_lsp;
            recovery_time_lsp = lsp;
        }
        else lsp = recovery_time_lsp;

        // Can't assert during normal execution since a spurious wakeup
        // may have happened
        if (!arg_lsp && !lsp) continue;

        // Generate a graph on the fly. This can be done in the user threads
        // but that would slow down the application. Instead, let the helper
        // thread build the graph. For that, the helper thread will have to
        // traverse the undo log entries. Denote every TCS by a node with a
        // nodeid. The log structure headers should probably also contain
        // a thread id that can be used as a primary id of the nodes. Note
        // that a mechanism is reqd for identifying the nodes that belong to
        // a given thread. The latter is required so that we know how to
        // re-attach the headers after pruning the log entries.

        // Start from log structure header. Walk through the TCSes that are
        // present for a given thread. For every TCS, create a TCS-node. Every
        // TCS-node has two ids, a primary and a secondary one. The primary id
        // is the one assigned by the graph builder. The secondary id is the
        // logical thread id a given node belongs to. The primary id is unique
        // across all TCS-nodes while the secondary id is not. The nodes are
        // built only for complete TCSes, so no new outgoing edge is possible.
        // But a new incoming edge is possible. But the question is whether we
        // care about such a new incoming edge? It appears that we don't, since
        // such an incoming edge will be from a TCS that is not currently under
        // consideration by the helper thread. While traversing the undo logs,
        // if an outgoing edge is encountered, check whether there is node-info
        // for the target undo log. If yes, create a new edge if there isn't
        // already one for this pair of TCS-nodes. If no, place the source
        // undo log in a pending-list. Only release-type entry logs can have
        // an incoming edge. So if a release-type entry log is encountered,
        // add a mapping from lock_addr -> (primary id, secondary id) in
        // the node-info data structure.

        // Nothing in the individual LogStructure entries should be changed
        // by any thread other than the helper (once they have been created).
        // New entries may be added by other threads but they are at the head,
        // so they won't be seen until the next round.

        // For all complete TCSes, build a graph. As the helper thread gathers
        // TCSes, more complete TCSes may be added to the log structure. Some
        // of these new ones may be ignored but this is ok since these new
        // TCSes are added after the ones considered in this phase. So edges
        // may exist from the new TCSes to the considered TCSes but that will
        // neither lead to corruption nor lead to incorrect consistency
        // detection. This is under the assumption that versioning is
        // performed for the log structure as outlined in the design document.

        // BTW, this is a graph with happens-after edges. Happens-after needs
        // to be defined.
        DirectedGraph dg;
        PendingList pl;
        NodeInfoMap nim;
        Log2Bool acq_map;
        Log2Bool rel_map;
        OcsMap ocs_map;
        
        // TODO: it is unclear whether we need the stable bit

        // TODO: we need to start from the oldest version around. This is
        // because even if a TCS is marked deleted but has not been destroyed,
        // there may be a reference to it, so we need to traverse those
        // deleted TCSes as well. We need to do this extra work because
        // without it, we do not know if a target is deleted or just absent.
#ifdef _PROFILE_HT
        start_graph_build = atlas_rdtsc();
#endif
        while (lsp)
        {
 
            ThreadContextPtr current_thr_context = LookUpThreadContext((void*) lsp);
            // todo remove sid
            intptr_t sid = -1;

            LogEntry *current_le = lsp->Le_;
            if (!current_le) continue;

            // This is used to maintain the previous node created
            VDesc prev_nid = 0;
            OcsMarker *prev_ocs = 0;
            bool is_first_node = true;
            uint32_t ocs_count = 0;
            while (true)
            {
                ++ocs_count;
                // TODO: If a consistent state is not found, the number
                // of FASEs examined should be increased.

                // TODO: Theoretically, there is a pattern for which new
                // consistent states cannot be found. That needs to be
                // somehow handled. 
                if (ocs_count > HELPER_OCS_ANALYSIS_LIMIT) break;
                
                if (ALAR(all_done))
                {
                    is_parent_done = true;
                    break;
                }
                 
                OcsMarker *current_ocs = CreateOcsMarker(current_thr_context, current_le);
                if (!current_ocs) break; // this thread is done
                else
                {
                    AddOcsMarkerToMap(&ocs_map, lsp, current_ocs);
                    AddOcsMarkerToVec(&ocs_vec, current_ocs);
                }

                if (prev_ocs) prev_ocs->Next_ = current_ocs;
                
                VDesc nid = CreateNode(&dg, current_ocs, sid);
                
                if (!is_first_node)
                {
                    assert(prev_nid);
                    CreateEdge(&dg, nid, prev_nid);
                }

                is_first_node = false;
                prev_nid = nid;
                do
                {
                    // TODO Currently, we are adding an edge here even if
                    // the pointer is to a log entry in the same thread.
                    // This should be changed in order to make the graph
                    // smaller and simpler.


                    if (isAcquire(current_le) && current_le->ValueOrPtr_)
                    {
                        LogEntry *rel_le =
                            (LogEntry*)(current_le->ValueOrPtr_);
                        // Note: the following call deletes the found entry
                        // ValueOrPtr_ is currently not of atomic type. This
                        // is still ok as long as there is a single helper
                        // thread. Change it to atomic for parallel helper.
                        if (isDeletedByHelperThread(rel_le, current_le->Size_))
                            current_le->ValueOrPtr_ = 0;
                        else if (Is_Recovery && !isFoundInExistingLog(
                                     rel_le, current_le->Size_,
                                     existing_rel_map))
                            current_le->ValueOrPtr_ = 0;
                    }
                        
                    if (isAcquire(current_le) && current_le->ValueOrPtr_)
                    {
                        TrackLog(current_le, &acq_map);
                        pair<VDesc,NODE_TYPE> ni_pair = GetTargetNodeInfo(
                            dg, nim, (LogEntry *)current_le->ValueOrPtr_);
                        if (ni_pair.second == NT_avail)
                            CreateEdge(&dg, nid, ni_pair.first);
                        else if (ni_pair.second == NT_absent)
                            AddToPendingList(&pl, current_le, nid);
                        // if target is already deleted, skip
                    }
                    else if (isAcquire(current_le))
                    {
                        TrackLog(current_le, &acq_map);
                    }
                    else if (isRelease(current_le))
                    {
                        AddToNodeInfoMap(
                            &nim, current_le, nid, sid, NT_avail);
                        // TODO. Note that rel_map is cumulative and
                        // hence may get really large. It needs to be
                        // pruned at some point of time.
                        TrackLog(current_le, &rel_map);
                    }
                    if (current_le == current_ocs->Last_) break;
                    // No need for an atomic read, we are guaranteed
                    // at this point that the next ptr won't change.
                    current_le = current_le->Next_;
                }while (true);
                prev_ocs = current_ocs;
                current_le = (LogEntry*) ALAR(current_le->Next_);
            }
            if (is_parent_done) break;
            lsp = lsp->Next_;
        }
#ifdef _PROFILE_HT
        end_graph_build = atlas_rdtsc();
        total_graph_build += end_graph_build - start_graph_build;
#endif        
        
        if (is_parent_done) break;

        UtilTrace4(helper_trace_file,
                  "Initial graph (%ld nodes): Iteration # %d\n",
                  num_vertices(dg), iter_num);
        GraphTrace(dg);
        
        if (!num_vertices(dg)) break;
        
#ifdef _PROFILE_HT
        start_graph_resolve = atlas_rdtsc();
#endif        
        // Now that all relevant TCSes have been processed, visit the pending
        // list and add all possible edges
        ResolvePendingList(&pl, nim, &dg, log_v);

        UtilTrace4(helper_trace_file,
                  "Initial graph after pending list resolution (%ld nodes): Iteration # %d\n",
                  num_vertices(dg), iter_num);
        GraphTrace(dg);
        
        if (ALAR(all_done))
        {
            is_parent_done = true;
            break;
        }
        
        // This denotes the vector of log entries that need modification
        // when a version removal occurs
        LogEntryVec le_vec;
        
        // TODO the collection of to_be_nullified acquires needs to happen
        // here. Currently, it is too late.
        RemoveUnresolvedNodes(&dg, pl, &le_vec, &acq_map, &rel_map);

        if (ALAR(all_done))
        {
            is_parent_done = true;
            break;
        }

        UtilTrace4(helper_trace_file,
                  "Resolved graph (%ld nodes): Iteration # %d\n",
                  num_vertices(dg), iter_num);
        GraphTrace(dg);

        CreateVersions(dg, &log_v, le_vec, &rel_map, acq_map, ocs_map);

#ifdef _PROFILE_HT
        end_graph_resolve = atlas_rdtsc();
        total_graph_resolve += end_graph_resolve - start_graph_resolve;
#endif        
        
        if (ALAR(all_done))
        {
            is_parent_done = true;
            break;
        }

#ifdef _PROFILE_HT
        start_graph_prune = atlas_rdtsc();
#endif        

        DestroyLogs(&log_v);

        DestroyOcses(&ocs_vec);
        
#ifdef _PROFILE_HT
        end_graph_prune = atlas_rdtsc();
        total_graph_prune += end_graph_prune - start_graph_prune;
#endif        

    }while (!ALAR(all_done));

    // We want to return as quickly as possible (even if that means that
    // some memory is leaked) because the odds are that we are
    // exiting and so it does not matter.
//    DestroyOcses(&ocs_vec); // in case we broke out of the loop above
    Finalize_helper();

#ifdef _PROFILE_HT
    fprintf(stderr, "[Atlas] Total graph creation cycles = %ld\n",
            total_graph_build);
    fprintf(stderr, "[Atlas] Total graph resolve cycles  = %ld\n",
            total_graph_resolve);
    fprintf(stderr, "[Atlas] Total graph prune cycles    = %ld\n",
            total_graph_prune);
    extern uint64_t total_log_destroy_cycles;
    fprintf(stderr, "[Atlas] Total log destroy cycles    = %ld\n",
            total_log_destroy_cycles);
#endif    
    // TODO
    // Since the user threads are done, we should remove all the undo logs.
    // The log structure header should be atomically changed so as to be
    // marked for garbage collection. For this, we need a proper garbage
    // collection story.
    // TODO: Do we really need the above? At this point, we delete the
    // logs persistent region.
#ifdef NVM_STATS    
    fprintf(stderr, "[Atlas] Iteration count is %d\n", iter_num);
    fprintf(stderr, "[Atlas] ");
    NVM_PrintNumFlushes();
#endif    
    fprintf(stderr, "[Atlas] Removed %ld log entries\n", removed_log_count);
    return 0;
}


