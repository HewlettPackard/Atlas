#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "atlas_api.h"
#include "helper.h"

#include <thread>
#include <atomic>
#include <unordered_map>

#define HELPER_OCS_ANALYSIS_LIMIT 8

#if defined(_FLUSH_GLOBAL_COMMIT) && !defined(DISABLE_FLUSHES) && \
    !defined(_DISABLE_DATA_FLUSH)
SetOfInts *global_flush_ptr = 0;
#endif

// Stats
std::atomic<int> num_log_entries; // Num of log entries deleted
std::atomic<int> num_ocs; // Num of OCSes deleted
std::atomic<int> num_incomplete_ocs; // Num of incomplete OCS
std::atomic<int> total_le_destroyed; // Num of log entres destroyed


#define NUM_HELPER_THREADS 1 // Num of helper threads for log truncation
#define PARALLEL_DEBUG 1 // Control for debug couts
#define NUM_PER_ITER_PER_THREAD 1200 // Num of log entries considered per worker thread per iter

typedef std::unordered_map<LogEntry*, OcsMarker*> Log_Collection;

bool dump_initial_graph = false;
bool dump_resolved_graph = false;
bool dump_versions = false;
bool dump_dtor = false;

uint64_t removed_log_count = 0;

FILE *helper_trace_file = NULL;

// Returns the number of worker threads in the program
int get_num_threads(LogStructure* lsp) {
  int numThreads = 0;
  while(lsp) {
    numThreads++;
    lsp = lsp->Next_;
  }

  return numThreads;
}

// Distribute the logn entry heads among helpers
// Effectively assigns the different worker threads to helper threads
// Simple chunk allocation
void assign_helper_start_end_postions(LogStructure* lsp, int numThreads,
                                      LogStructure** helper_start_position,
                                      LogStructure** helper_end_position) {
  int num_worker_threads_per_helper = numThreads/NUM_HELPER_THREADS;
  int i=0;
  int j;
  for(; i<NUM_HELPER_THREADS; i++) {
    //if(PARALLEL_DEBUG) std::cout<<"i = "<<i<<" j = "<<j<<std::endl;
    helper_start_position[i] = lsp;
    for(j=1; j<num_worker_threads_per_helper; j++) {
      //if(PARALLEL_DEBUG) std::cout<<"i = "<<i<<" j = "<<j<<std::endl;
      lsp = lsp->Next_;
    }
    helper_end_position[i] = lsp;
    assert(lsp);
    lsp = lsp->Next_;
  }
  while(lsp) {
    //if(PARALLEL_DEBUG) std::cout<<"entered addiiontal"<<std::endl;
    helper_end_position[i-1] = lsp;
    lsp = lsp->Next_;
  }
}

// Distribute the OCSes heads among helpers
// We create an OCS head for each worker thread, mapping the ocs heads to helper thread
// We later use these ocs heads to create OCSes for the worker threads
void assign_ocs_heads_positions(OcsMarker* ocs_heads, int numThreads,
                                OcsMarker** helper_ocs_start_pos,
                                OcsMarker** helper_ocs_end_pos) {
  int num_worker_threads_per_helper = numThreads/NUM_HELPER_THREADS;
  int num_assigned_threads=0;
  int i=0;
  int j;
  for(; i<NUM_HELPER_THREADS; i++) {
    helper_ocs_start_pos[i] = ocs_heads;
    num_assigned_threads++;
    //if(PARALLEL_DEBUG) std::cout<<"assigned "<<ocs_heads->id<<" to "<<i<<", "<<j<<std::endl;
    for(j=1; j<num_worker_threads_per_helper; j++) {
      ocs_heads++;
      num_assigned_threads++;
      //if(PARALLEL_DEBUG) std::cout<<"assigned "<<ocs_heads->id<<" to "<<i<<", "<<j<<std::endl;
    }
    helper_ocs_end_pos[i] = ocs_heads;

    ocs_heads++;
  }
  while(num_assigned_threads< numThreads) {
    //if(PARALLEL_DEBUG) std::cout<<"ented addiiontal"<<std::endl;
    //if(PARALLEL_DEBUG) std::cout<<"ocs to be assigned "<<ocs_heads<<std::endl;
    //if(PARALLEL_DEBUG) std::cout<<"old end pos "<<helper_ocs_end_pos[i]<<" "<<std::endl;
    helper_ocs_end_pos[i-1] = ocs_heads;
    //if(PARALLEL_DEBUG) std::cout<<"assigned "<<ocs_heads->id<<" to "<<i<<", "<<j<<std::endl;
    //if(PARALLEL_DEBUG) std::cout<<"new end pos "<<helper_ocs_end_pos[i]<<" "<<std::endl;
    num_assigned_threads++;
    ocs_heads++;
  }
}

// Go through the log entries of the worker threads and entrer them into the log_entry_collection map
// One all the helper threads are done with this step, the log entry collection table represents all
// the log entries being considered for truncation this iteration.
// The log entries in the tale is called "global consistent state".
void create_global_consistent_state(LogStructure* helper_start_position,
                                    LogStructure* helper_end_position,
                                    OcsMarker* helper_ocs_start_pos,
                                    std::unordered_map<LogEntry*, OcsMarker*>* log_entry_collection_ptr) {
  LogStructure* lsp = helper_start_position;
  OcsMarker* current_ocs_head = helper_ocs_start_pos;
  bool is_new_ocs = true;
  OcsMarker* prev_ocs = NULL;;
  OcsMarker* current_ocs = NULL;
  OcsMarker* next_ocs = NULL;

  assert(!((*log_entry_collection_ptr).size()));

  while(true) {
    LogEntry* current_le = lsp->Le_;
    prev_ocs = current_ocs_head;
    int lock_count = 0;
    int num_log_entries_curr_iter = 0;
    bool is_first_ocs = true;
    bool created_next_ocs = false;
    //if (PARALLEL_DEBUG) std::cout<<"****** New thread ******"<<std::endl;
    //if (PARALLEL_DEBUG) std::cout<<"lsp = "<<lsp<<" current_le = "<<current_le<<std::endl;
    ////// Assumption: considering the last entry in the log as well, ok?
    while(current_le && num_log_entries_curr_iter<NUM_PER_ITER_PER_THREAD) {
    //  if(PARALLEL_DEBUG)std::cout<<"LE: "<<current_le<<" Type: ";
    //  
    //  if(isAcquire(current_le)) {
    //   if(PARALLEL_DEBUG) std::cout<<"Acq";
    //  }
    //  else if(isRelease(current_le)) {
    //   if(PARALLEL_DEBUG) std::cout<<"Rel";
    //  }
    //  else if(isBeginDurable(current_le)) {
    //   if(PARALLEL_DEBUG) std::cout<<"BgnDur";
    //  }
    //  else if(isEndDurable(current_le)) {
    //   if(PARALLEL_DEBUG) std::cout<<"EndDur";
    //  }
    //  else
    //    if(PARALLEL_DEBUG)std::cout<<"Data";

    //  if(PARALLEL_DEBUG)std::cout<<" lock count = "<<lock_count;

  
    //if(PARALLEL_DEBUG) std::cout<<" Value: "<<current_le->ValueOrPtr_<<std::endl;

      if(is_first_ocs) {
        //if(PARALLEL_DEBUG)std::cout<<"*** new ocs ***"<<std::endl;
        current_ocs = new OcsMarker;
        prev_ocs->Next_ = current_ocs;
        current_ocs->First_ = current_le;
        current_ocs->canBeDeleted_ = false;
        current_ocs->Next_ = NULL;
        is_first_ocs = false;
      }
      //Assuming the first entry wont be a release, might need the help of a compiler
      if(isRelease(current_le) || isEndDurable(current_le)) { //Assumption: No reader writer locks
        lock_count--;
        if(lock_count == 0) {
          //if(PARALLEL_DEBUG)std::cout<<"*** isr: new ocs ***"<<std::endl;
          current_ocs->Last_ = current_le;
          current_ocs->isDeleted_ = false;
          current_ocs->canBeDeleted_ = true; // may be set to false if find inconsistencies later
          num_ocs++;

          next_ocs = new OcsMarker;
          current_ocs->Next_ = next_ocs;
          next_ocs->First_ = current_le->Next_;
          next_ocs->canBeDeleted_ = false;
          next_ocs->Next_ = NULL;
          created_next_ocs = true;

          prev_ocs = current_ocs;
        }
      }

      //if(lock_count==0 && is_new_ocs) {
      //  std::cout<<"*** new ocs ***"<<std::endl;
      //  current_ocs = new OcsMarker;
      //  prev_ocs->Next_ = current_ocs;
      //  current_ocs->First_ = current_le;
      //  current_ocs->canBeDeleted_ = false;
      //  current_ocs->Next_ = NULL;
      //  is_new_ocs = false;
      //  num_ocs++;
      //  if(isAcquire(current_le) || isBeginDurable(current_le)) { //Assumption: No reader writer locks
      //    lock_count++;
      //  }
      //}
      //else if(lock_count == 0 && !is_new_ocs) {
      //  current_ocs->Last_ = current_le;
      //  current_ocs->isDeleted_ = false;
      //  current_ocs->canBeDeleted_ = true; // may be set to false if find inconsistencies later
      //  prev_ocs = current_ocs;
      //  is_new_ocs = true;
      //}
      else if(isAcquire(current_le) || isBeginDurable(current_le)) {
        lock_count++; // Assumption: No reader/writer locks
      }


      //if(PARALLEL_DEBUG) std::cout<<"seen log entry = "<<current_le<<std::endl;
      assert((*log_entry_collection_ptr).find(current_le) == (*log_entry_collection_ptr).end());
      (*log_entry_collection_ptr)[current_le] = current_ocs;
      //if(PARALLEL_DEBUG)std::cout<<"LE, OCS pairing: LE: "<<current_le<<" oCS: "<<current_ocs<<std::endl;
      current_le = current_le->Next_;
      if (created_next_ocs) {
          assert(current_ocs != next_ocs);
          current_ocs = next_ocs;
          created_next_ocs = false;
      }
      num_log_entries++;
      num_log_entries_curr_iter++;
    }
    
    if(lsp == helper_end_position) break;
    lsp = lsp->Next_;
    current_ocs_head++;
  }
}

// Returns if the log entry is present in the gloal consistent state
bool is_present_in_global_state(LogEntry* current_le, 
                            std::unordered_map<LogEntry*, OcsMarker*>* log_entry_collection_ptr) {
  for(int i=0; i<NUM_HELPER_THREADS; i++) {
    if((*(log_entry_collection_ptr+i)).find(current_le) != (*(log_entry_collection_ptr+i)).end()) {
      return true;
    }
  }
  return false;
}

// Returns a pointer to the OCS of the log entry.
OcsMarker* find_ocs_of_log_entry(LogEntry* current_le, 
                            std::unordered_map<LogEntry*, OcsMarker*>* log_entry_collection_ptr) {
  for(int i=0; i<NUM_HELPER_THREADS; i++) {
    if((*(log_entry_collection_ptr+i)).find(current_le) != (*(log_entry_collection_ptr+i)).end()) {
      return (*(log_entry_collection_ptr+i))[current_le];
    }
  }
  return NULL;
}

// Has the log entry been already deleted in previos iterations?
bool is_already_deleted_by_helpers(LogEntry* current_le,
                               std::unordered_map<LogEntry*, int>* deleted_log_entry_collection_ptr) {
  for(int i=0; i<NUM_HELPER_THREADS; i++) {
    std::unordered_map<LogEntry*, int>*current_deleted_log_entry_collection_ptr = 
                                                      deleted_log_entry_collection_ptr+i;
    if((*current_deleted_log_entry_collection_ptr).find(current_le) != (*current_deleted_log_entry_collection_ptr).end()) {
      // Log entry has been deleted
      assert((*current_deleted_log_entry_collection_ptr)[current_le] == 1);
      (*current_deleted_log_entry_collection_ptr)[current_le] = 2;
      // no body will ever look for it, can be deleted
      return true;
    }
  }
  return false;

}

// Reverse the happens-before dependencies in the acq-rel log entries
void reverse_dependencies(int num_threads, int helper_id,
                          LogStructure* helper_start_position,
                          LogStructure* helper_end_position,
                          std::unordered_map<LogEntry*, OcsMarker*>* log_entry_collection_ptr,
                          //std::unordered_map<LogEntry*, bool>* helper_causes_incomplete_ocs,
                          std::unordered_map<LogEntry*, int>* deleted_log_entry_collection_ptr) {
  LogStructure* lsp = helper_start_position;
  std::unordered_map<LogEntry*, OcsMarker*>* my_log_entry_collec_ptr = log_entry_collection_ptr+helper_id;
  bool found_inconsistent_acquire = false;
  bool is_last_entry = false;
  //std::unordered_map<LogEntry*, bool> &current_causes_incomplete_ocs = *(helper_causes_incomplete_ocs);
  while(true) {
    if(lsp == helper_end_position) break;
    LogEntry* current_le = lsp->Le_;

    while(current_le) {
      if((*my_log_entry_collec_ptr).find(current_le) != (*my_log_entry_collec_ptr).end()) break;

      if(isAcquire(current_le) && current_le->ValueOrPtr_) {
        LogEntry* priorRel = (LogEntry*)current_le->ValueOrPtr_;
        if(is_present_in_global_state(priorRel, log_entry_collection_ptr)) {
          // Prior release is present in current global state, just add rev dep
          assert(isRelease(priorRel));
          priorRel->ValueOrPtr_ = (uintptr_t) current_le; // is this typecast alright?
        }
        else if(!is_already_deleted_by_helpers(priorRel, deleted_log_entry_collection_ptr)) {
          // Implies, release node is so young, it got created after global consistent
          // state was formed, mark the acquire as causing an incomplete OCS
          found_inconsistent_acquire = true; // Once set to true, will remain true for all subseq ocs
        }
      }
      if(!current_le->Next_)
        is_last_entry = true;


      // Dont think we need to check canBeDeleted in if below
      if((found_inconsistent_acquire && (*my_log_entry_collec_ptr)[current_le]->canBeDeleted_) || is_last_entry) {
        (*my_log_entry_collec_ptr)[current_le]->canBeDeleted_ = false;
      }

      current_le = current_le->Next_;
    }
    lsp = lsp->Next_;
  }
}

// Find all the OCSes dependent on the current one
void find_dpndnt_ocs(OcsMarker* current_ocs, std::vector<OcsMarker*>* all_dpndnt_ocs,
                     std::unordered_map<LogEntry*, OcsMarker*>* log_entry_collection_ptr) {
  LogEntry* current_le = current_ocs->First_;

  (*all_dpndnt_ocs).push_back(current_ocs);

  while(current_le) {

    if(current_le && isRelease(current_le) && current_le->ValueOrPtr_) {
      OcsMarker* dpndnt_ocs = find_ocs_of_log_entry(current_le, log_entry_collection_ptr);
      bool was_ocs_already_visited = false;
      for(int i=0; i<(*all_dpndnt_ocs).size(); i++) {
        if((*all_dpndnt_ocs)[i] == dpndnt_ocs) {
          was_ocs_already_visited = true;
          break;
        }
      }
      if(!was_ocs_already_visited)
        find_dpndnt_ocs(dpndnt_ocs, all_dpndnt_ocs, log_entry_collection_ptr);
    }

    if(current_le == current_ocs->Last_) break;
    current_le = current_le->Next_;
  }

}

// Mark all the incomplete ocses in the global consistent state
void mark_all_incomplete_ocs(OcsMarker* helper_ocs_start_pos, OcsMarker* helper_ocs_end_pos,
                              std::unordered_map<LogEntry*, OcsMarker*>* log_entry_collection_ptr) {
  OcsMarker* current_ocs_head = helper_ocs_start_pos;

  while(true) {

    OcsMarker* current_ocs = current_ocs_head->Next_;
    //if(PARALLEL_DEBUG) std::cout<<"starting for worker id: "<<worker_id++<<std::endl;


    //ocs_id = 0;
    while(current_ocs) {
      assert(current_ocs);
      //ocs_id++;
      if(!current_ocs->canBeDeleted_) {
        //if(PARALLEL_DEBUG) std::cout<<"found an incomplete ocs at id: "<<ocs_id<<std::endl;
        std::vector<OcsMarker*> all_dpndnt_ocs;
        find_dpndnt_ocs(current_ocs, &all_dpndnt_ocs, log_entry_collection_ptr);
        while(!all_dpndnt_ocs.empty()) {
          num_incomplete_ocs++;
          OcsMarker* dpndnt_ocs = all_dpndnt_ocs.back();
          if(!current_ocs->canBeDeleted_) {
            dpndnt_ocs->canBeDeleted_ = false;
          }
          all_dpndnt_ocs.pop_back();
        }
      }
      current_ocs = current_ocs->Next_;
    }

    if(current_ocs_head == helper_ocs_end_pos) break;
    current_ocs_head++;
  }
  
}


// Create a set of log heads so that we can atomically switch to these new log heads
// Effectively, atomically truncating the log
void create_new_log_structure_heads(OcsMarker* helper_ocs_start_pos,
                                    OcsMarker* helper_ocs_end_pos,
                                    LogStructure* helper_new_start_position,
                                    atomic<int>* num_ocs_deleted) {
  OcsMarker* current_ocs_head = helper_ocs_start_pos;
  LogStructure* current_new_start_position = helper_new_start_position;

  int worker_id = 0;
  int ocs_id = 0;
  while(true) {
    //if(PARALLEL_DEBUG) std::cout<<"worker id: "<<worker_id++<<std::endl;
    OcsMarker* current_ocs = current_ocs_head->Next_;
    ocs_id = 0;
    while(current_ocs && current_ocs->Next_) {
        //if(PARALLEL_DEBUG) std::cout<<"at ocs_id: "<<ocs_id++<<std::endl;
        //if(PARALLEL_DEBUG) std::cout<<"both current and next valid"<<std::endl;
        if(current_ocs->canBeDeleted_) {
          //if(PARALLEL_DEBUG) std::cout<<"ocs can be deleted"<<std::endl;
          (*num_ocs_deleted)++;
          //if(PARALLEL_DEBUG) std::cout<<"num_ocs_deleted = "<<*num_ocs_deleted<<std::endl;
          current_new_start_position->Le_ = current_ocs->Next_->First_;
        } else {
          break;
        }
        current_ocs = current_ocs->Next_;
    }
    if(current_ocs_head == helper_ocs_end_pos) break;
    current_ocs_head++;
    current_new_start_position = current_new_start_position->Next_;
  }
}

// Destroy the deleted log entries and OCSes
void destroy_logs_and_ocs(std::unordered_map<LogEntry*, int>* helper_deleted_log_entry_collection_ptr,
                          OcsMarker* helper_ocs_start_pos, OcsMarker* helper_ocs_end_pos,
                          atomic<int>* num_le_destroyed) {
  OcsMarker* current_ocs_head = helper_ocs_start_pos;

  while(true) {
    OcsMarker* current_ocs = current_ocs_head->Next_;
    while(current_ocs) {
      LogEntry* current_le = current_ocs->First_;
      while(current_ocs->canBeDeleted_) {
        LogEntry* temp = current_le;
        //if(PARALLEL_DEBUG) std::cout<<"Destroying LE: "<<current_le<<" from OCS: "<<current_ocs<<std::endl;
        DestroyLogEntry(current_le);
        assert((*helper_deleted_log_entry_collection_ptr).find(current_le) == (*helper_deleted_log_entry_collection_ptr).end());
        (*helper_deleted_log_entry_collection_ptr)[current_le] = 1;
        (*num_le_destroyed)++;
        total_le_destroyed++;
        if(current_le == current_ocs->Last_) break;
        current_le = current_le->Next_;
      }
      OcsMarker* temp = current_ocs;
      current_ocs = current_ocs->Next_;

      delete temp;
    }

    if(current_ocs_head == helper_ocs_end_pos) break;
    current_ocs_head++;
  }

  std::unordered_map<LogEntry*, int>::iterator it = (*helper_deleted_log_entry_collection_ptr).begin();
  while(it != (*helper_deleted_log_entry_collection_ptr).end()) {
    if(it->second == 2) {
      (*helper_deleted_log_entry_collection_ptr).erase(it++);
    } else {
      it++;
    }
  }
}

void print_ocs(OcsMarker* ocs_heads) {
  OcsMarker* end_ocs_head = ocs_heads+1;
  while(true) {
    OcsMarker* current_ocs = ocs_heads->Next_;
    std::cout<<"********************* New Thread ******************"<<std::endl;
    while(current_ocs) {
      std::cout<<"*** New Ocs ***"<<std::endl;
      current_ocs = current_ocs->Next_;
      if(!current_ocs) break;
      std::cout<<" Ocs: "<<current_ocs<<" first: "<<current_ocs->First_<<" last: "<<current_ocs->Last_;
      if(current_ocs->canBeDeleted_) {
        std::cout<<" canBeDeleted: Yes";
      } else {
        std::cout<<" canBeDeleted: No";
      }

      std::cout<<std::endl;
    }
    if(ocs_heads == end_ocs_head) break;
    ocs_heads++;
  }
}

void run_parallel_helper(int helper_id, int num_threads, atomic<int>* barrier, atomic<int>* num_ocs_deleted,
                                 atomic<int>* num_le_destroyed,
                                 LogStructure* helper_start_position,
                                 LogStructure* helper_end_position,
                                 LogStructure* helper_new_start_position,
                                 LogStructure* helper_new_end_position,
                                 OcsMarker* helper_ocs_start_pos,
                                 OcsMarker* helper_ocs_end_pos,
                                 //std::unordered_map<LogEntry*, bool>* causes_incomplete_ocs,
                                 std::unordered_map<LogEntry*, OcsMarker*>* log_entry_collection_ptr,
                                 std::unordered_map<LogEntry*, int>* deleted_log_entry_collection_ptr) {

  // Create thread-local data
  std::unordered_map<LogEntry*, OcsMarker*>* helper_log_entry_collection_ptr = log_entry_collection_ptr+helper_id;
  std::unordered_map<LogEntry*, int>* helper_deleted_log_entry_collection_ptr =
                                           deleted_log_entry_collection_ptr+helper_id;

  // Assert that the helper's log entry collection is empty
  assert(!((*helper_log_entry_collection_ptr).size()));

  // Add all the log entries to the global consistent state.
  // Create OCSes for each of the worker threads
  create_global_consistent_state(helper_start_position, helper_end_position,
                                 helper_ocs_start_pos, helper_log_entry_collection_ptr);


  if (NUM_HELPER_THREADS > 1) {
    (*barrier)++;
    while(*barrier < NUM_HELPER_THREADS+1) {;} // Wait for all the helpers to create a consistent global state
  }

  // Worker threads record dependencies from acq<-rel, for the parallel algorithm,
  // we use the dependencies from rel->acq, this step records the reverse dependences
  reverse_dependencies(num_threads, helper_id,
                                  helper_start_position, helper_end_position,
                                  log_entry_collection_ptr,
                                  //helper_causes_incomplete_ocs,
                                  deleted_log_entry_collection_ptr);

  //if(PARALLEL_DEBUG) std::cout<<"finished rev deps"<<std::endl;

  //if(PARALLEL_DEBUG) std::cout<<"done reversing dependencies"<<std::endl;
  //if(PARALLEL_DEBUG) print_ocs(helper_ocs_start_pos);
  if (NUM_HELPER_THREADS > 1) {
    (*barrier)++;
    while(*barrier < 2*NUM_HELPER_THREADS+1) {;} // Wait for all the helpers to finish recording
                                             // reverse dependencies
  }

  // Mark all hte incomplete OCSes in the global consistent state
  mark_all_incomplete_ocs(helper_ocs_start_pos, helper_ocs_end_pos, log_entry_collection_ptr);

  //std::cout<<"done marking incomplete ocs"<<std::endl;
  //print_ocs(helper_ocs_start_pos);
  if (NUM_HELPER_THREADS > 1) {
    (*barrier)++;
    while(*barrier < 3*NUM_HELPER_THREADS+1) {;} // Wait for all the helpers to mark the incomplete OCSes
  }

  // Based on the incomplete OCSes, update the new log entry heads
  create_new_log_structure_heads(helper_ocs_start_pos, helper_ocs_end_pos, helper_new_start_position, num_ocs_deleted);
  //if(PARALLEL_DEBUG) std::cout<<"done creating new heads"<<std::endl;
  //print_ocs(helper_ocs_start_pos);
  if (NUM_HELPER_THREADS > 1) {
    (*barrier)++;
    while(*barrier < 4*NUM_HELPER_THREADS+1) {;}    // Wait for all the helpers to update the new log
  }                                              // entry heads

  if (NUM_HELPER_THREADS > 1) {
    while(*barrier < 4*NUM_HELPER_THREADS+2) {;}  // Wait for the main helper to CAS the lsp to point
  }                                              // to new log entry heads

  if (NUM_HELPER_THREADS > 1) {
    destroy_logs_and_ocs(helper_deleted_log_entry_collection_ptr,
                          helper_ocs_start_pos, helper_ocs_end_pos, num_le_destroyed);
  }

}


void print_log_status(OcsMarker* ocs_heads, LogStructure* lsp, int numThreads,
                      std::unordered_map<LogEntry*, OcsMarker*>* log_entry_collection_ptr) {

  OcsMarker* end_ocs_head = ocs_heads+numThreads-1;
  //while(true) {
  //  OcsMarker* current_ocs = ocs_heads->Next_;
  //  std::cout<<"********************* New Thread ******************"<<std::endl;
  //  while(current_ocs) {
  //    std::cout<<"*** New Ocs ***"<<std::endl;
  //    current_ocs = current_ocs->Next_;
  //    if(!current_ocs) break;
  //    std::cout<<" Ocs: "<<current_ocs<<" first: "<<current_ocs->First_<<" last: "<<current_ocs->Last_;
  //    if(current_ocs->canBeDeleted_) {
  //      std::cout<<" canBeDeleted: Yes";
  //    } else {
  //      std::cout<<" canBeDeleted: No";
  //    }

  //    std::cout<<std::endl;
  //    //LogEntry* current_le = current_ocs->First_;
  //    //while(current_le) {
  //    //  std::cout<<"LE: "<<current_le<<" Type: ";
  //    //  
  //    //  if(isAcquire(current_le))
  //    //    std::cout<<"Acq";
  //    //  else if(isRelease(current_le))
  //    //    std::cout<<"Rel";
  //    //  else if(isBeginDurable(current_le))
  //    //    std::cout<<"BgnDur";
  //    //  else if(isEndDurable(current_le))
  //    //    std::cout<<"EndDur";
  //    //  else
  //    //    std::cout<<"Data";
  //
  //    //  std::cout<<" Value: "<<current_le->ValueOrPtr_;

  //    //  OcsMarker* log_collec_ocs = find_ocs_of_log_entry(current_le, log_entry_collection_ptr);
  //    //  std::cout<<" Ocs: "<<current_ocs<<" LogCollecOcs: "<<log_collec_ocs;

  //    //  if(log_collec_ocs) {
  //    //    if(current_ocs == log_collec_ocs)
  //    //      std::cout<<" Same"<<std::endl;
  //    //    else
  //    //      std::cout<<" Different"<<std::endl;
  //    //  }
  //    //  else {
  //    //    std::cout<<" Inconsistent"<<std::endl;
  //    //  }
  //
  //    //  if(current_le == current_ocs->Last_) break;
  //    //  current_le = current_le->Next_;
  //    //}
  //  }
  //  if(ocs_heads == end_ocs_head) break;
  //  ocs_heads++;
  //}

  while(lsp) {
    LogEntry* current_le = lsp->Le_;
    std::cout<<"********************* New Thread ******************"<<std::endl;

    int lock_count = 0;
    while(current_le) {
      if(lock_count == 0)
        std::cout<<"*** New Ocs ***"<<std::endl;
      std::cout<<"LE: "<<current_le<<" Type: ";
      
      if(isAcquire(current_le)) {
        std::cout<<"Acq";
        lock_count++;
      }
      else if(isRelease(current_le)) {
        std::cout<<"Rel";
        lock_count--;
      }
      else if(isBeginDurable(current_le)) {
        std::cout<<"BgnDur";
        lock_count++;
      }
      else if(isEndDurable(current_le)) {
        std::cout<<"EndDur";
        lock_count--;
      }
      else
        std::cout<<"Data";

  
      std::cout<<" Value: "<<current_le->ValueOrPtr_;

      OcsMarker* log_collec_ocs = find_ocs_of_log_entry(current_le, log_entry_collection_ptr);
      std::cout<<" LogCollecOcs: "<<log_collec_ocs<<std::endl;

      current_le = current_le->Next_;
    }

    lsp = lsp->Next_;
  }
}

void *parallel_helper() {

  int iter_num = 0;
  bool is_parent_done = false;

  LogStructure* lsp = NULL;

  std::unordered_map<LogEntry*, int>* deleted_log_entry_collection = 
                      new std::unordered_map<LogEntry*, int>[NUM_HELPER_THREADS];

  total_le_destroyed = 0;

  do {
    
    uint64_t start_run_helper, done_setup, done_helper, done_atomic, done_destroying, done_dealloc = 0;
    start_run_helper = atlas_rdtsc();

    num_log_entries = 0;
    num_ocs = 0;
    num_incomplete_ocs = 0;
    iter_num++;

    //if(PARALLEL_DEBUG) std::cout<<"*** It No. "<<iter_num++<<" ***"<<std::endl;
    //if(PARALLEL_DEBUG) std::cout<<"*** new iteration ***"<<std::endl;

    // ***Not sure about this part, just letting it be the same as the single threaded code
    pthread_mutex_lock(&helper_lock);
    if (!ALAR(all_done))
      pthread_cond_wait(&helper_cond, &helper_lock);
    pthread_mutex_unlock(&helper_lock);

    if(ALAR(all_done)) { 
      break;
    }

    // New parallel helper begins here
    lsp = (LogStructure*) ALAR(*log_structure_header_p);
    if(!lsp) continue;

    std::atomic<int> barrier; barrier = 0;
    std::atomic<int> num_ocs_deleted; num_ocs_deleted = 0;
    std::atomic<int> num_le_destroyed; num_le_destroyed = 0;

    std::thread helpers[NUM_HELPER_THREADS];

    // Ocs heads blonging to different helpers
    OcsMarker** helper_ocs_start_pos = new OcsMarker*[NUM_HELPER_THREADS];
    OcsMarker** helper_ocs_end_pos = new OcsMarker*[NUM_HELPER_THREADS];
  
    // Log entry heads belonging to different helpers
    LogStructure** helper_start_position = new LogStructure*[NUM_HELPER_THREADS];
    LogStructure** helper_end_position = new LogStructure*[NUM_HELPER_THREADS];

    // Log entry heads belonging to different helpers, after the log pruning
    LogStructure** helper_new_start_position = new LogStructure*[NUM_HELPER_THREADS];
    LogStructure** helper_new_end_position = new LogStructure*[NUM_HELPER_THREADS];

    // log entry collection, for creating global consistent state
    std::unordered_map<LogEntry*, OcsMarker*>* log_entry_collection = new std::unordered_map<LogEntry*, OcsMarker*>[NUM_HELPER_THREADS];

    //std::unordered_map<LogEntry*, bool>* causes_incomplete_ocs = new std::unordered_map<LogEntry*, bool>[NUM_HELPER_THREADS];


    // Assumption: numThreads is a multiple of num_helper_threads
    int numThreads = get_num_threads(lsp); // get the number of worker threads

    OcsMarker* ocs_heads = new OcsMarker[numThreads]; // create ocs heads for each worker thread
    // Initialize the ocs heads
    for(int i=0; i<numThreads; i++) {
      ocs_heads[i].id = i;
      ocs_heads[i].First_ = NULL;
      ocs_heads[i].Last_ = NULL;
      ocs_heads[i].Next_ = NULL;
      //if(PARALLEL_DEBUG) std::cout<<"ocs ids: "<<ocs_heads[i].id<<std::endl;
    }

    // Creating new log heads. The lsp will be changed to point to these new heads after log pruning.
    // Initializing the new log heads to be old log heads
    LogStructure* curr = NULL;
    LogStructure* prev = NULL;
    LogStructure* new_lsp = NULL; // the value to which the new lsp will be changed to
    LogStructure* lsp_copy = lsp;
    while(lsp_copy) {
      if(curr == NULL) {
        new_lsp = (LogStructure*) nvm_alloc(sizeof(LogStructure), nvm_logs_id); // nv alloc
        curr = new_lsp;
      }
      else {
        curr = (LogStructure*) nvm_alloc(sizeof(LogStructure), nvm_logs_id); // nv alloc
      }

      curr->Le_ = lsp_copy->Le_;
      curr->Next_ = NULL;
      if(prev != NULL)
        prev->Next_ = curr;
      prev = curr;

      //TODO: everything must be flushed out

      lsp_copy = lsp_copy->Next_;
    }

    // Distribute worker log-entry heads among helpers
    assign_helper_start_end_postions(lsp, numThreads,
                                          helper_start_position, helper_end_position);

    // Distribute new worker log-entry  heads among helpers
    assign_helper_start_end_postions(new_lsp, numThreads,
                                          helper_new_start_position, helper_new_end_position);

    // Distribute worker ocs-heads among helpers
    assign_ocs_heads_positions(ocs_heads, numThreads,
                                          helper_ocs_start_pos, helper_ocs_end_pos);

    //for(int i=0; i<NUM_HELPER_THREADS; i++) {
    //  std::cout<<"num threads: "<<numThreads<<" helper ids: "<<helper_ocs_start_pos[i]->id<<" "<<helper_ocs_end_pos[i]->id<<std::endl;
    //}

    // Spawn the helpers
  
    done_setup = atlas_rdtsc(); 
    if (NUM_HELPER_THREADS > 1) {
      for(int i=0; i<NUM_HELPER_THREADS; i++) {
        helpers[i] = thread(run_parallel_helper, i, numThreads, &barrier, &num_ocs_deleted, &num_le_destroyed,
                                             helper_start_position[i], helper_end_position[i],
                                             helper_new_start_position[i], helper_new_end_position[i],
                                             helper_ocs_start_pos[i], helper_ocs_end_pos[i],
                                             //causes_incomplete_ocs,
                                             log_entry_collection, deleted_log_entry_collection);
      }
    } else {
        run_parallel_helper(0, numThreads, &barrier, &num_ocs_deleted, &num_le_destroyed,
                                             helper_start_position[0], helper_end_position[0],
                                             helper_new_start_position[0], helper_new_end_position[0],
                                             helper_ocs_start_pos[0], helper_ocs_end_pos[0],
                                             //causes_incomplete_ocs,
                                             log_entry_collection, deleted_log_entry_collection);

    }
    done_helper = atlas_rdtsc();

    //while(barrier < NUM_HELPER_THREADS) {}


    //print_log_status(ocs_heads, lsp, numThreads, log_entry_collection);
    //barrier++;

    // Wait till the helpers have updated the new log entry heads
    //while(barrier < 4*NUM_HELPER_THREADS+1) {}

    //std::cout<<"************ Iteration No. "<<iter_num<<" *************"<<std::endl;

    //print_log_status(ocs_heads, lsp, numThreads, log_entry_collection);
    //LogStructure* temp_lsp = lsp;
    //while(temp_lsp) {
    //  std::cout<<"log struct header: "<<temp_lsp<<" LE_ = "<<temp_lsp->Le_<<std::endl;
    //  temp_lsp = temp_lsp->Next_;
    //}

    // Swap the lsp to point to the new log entry heads
    int num_log_entries_deleted = 0;
    while(true) {
      //std::cout<<"in lsp swap loop"<<std::endl;
      LogStructure* latest_worker_lsp = (LogStructure*) ALAR(*log_structure_header_p);
      bool did_cas_succeed = false;
      bool found_my_lsp = false;
      
      if(latest_worker_lsp == lsp) {
        LogStructure* old_val = (LogStructure*) CAS(log_structure_header_p, lsp, new_lsp);
        if(old_val == lsp)
          did_cas_succeed = true;
        //TODO flush
      }
      else {
        LogStructure* current_lsp = latest_worker_lsp;
        LogStructure* prev_lsp = NULL;
        while(current_lsp) {
          prev_lsp = current_lsp;
          current_lsp = current_lsp->Next_;
          if(current_lsp == lsp) {
            prev_lsp->Next_ = new_lsp; //TODO: Flush
            found_my_lsp = true;
            break;
          }
        }
        assert(found_my_lsp);
      }

      if(latest_worker_lsp == lsp) {
        if(did_cas_succeed)
          break;
      }
      else {
        assert(found_my_lsp);
        break;
      }
    }
    done_atomic = atlas_rdtsc();
    //temp_lsp = (LogStructure*) ALAR(*log_structure_header_p);
    //
    //while(temp_lsp) {
    //  std::cout<<"log struct header: "<<temp_lsp<<" LE_ = "<<temp_lsp->Le_<<std::endl;
    //  temp_lsp = temp_lsp->Next_;
    //}

    //print_log_status(ocs_heads, lsp, numThreads, log_entry_collection);
    //std::cout<<"************ Iteration No. "<<iter_num<<" *************"<<std::endl;

    // Signal the helper threads that they can now destroy the deleted log entries
    barrier++;


    // Wait for the helpers to be done
    if (NUM_HELPER_THREADS > 1) {
      for(int i=0; i<NUM_HELPER_THREADS; i++) {
        helpers[i].join();
      }
    } else {
      destroy_logs_and_ocs(deleted_log_entry_collection,
                          helper_ocs_start_pos[0], helper_ocs_end_pos[0], &num_le_destroyed);
    }

    done_destroying = atlas_rdtsc();

    //std::cout<<"num LEs: "<<num_log_entries<<" num ocs: "<<num_ocs<<" num incomplete ocs: "<<num_incomplete_ocs<<std::endl;

    delete[] helper_start_position;
    delete[] helper_end_position;
    delete[] helper_new_start_position;
    delete[] helper_new_end_position;
    delete[] log_entry_collection;
    delete[] ocs_heads;
    delete[] helper_ocs_start_pos;
    delete[] helper_ocs_end_pos;

    done_dealloc = atlas_rdtsc();

    std::cout<<" num LEs destroyed: "<<num_le_destroyed<<std::endl;
    std::cout<<"num OCS deleted: "<<num_ocs_deleted<<std::endl;
    std::cout<<"times: setup: "<<(done_setup-start_run_helper)<<" helper: "<<(done_helper-done_setup)<<" atomic: "<<(done_atomic-done_helper)<<" destroy: "<<(done_destroying-done_atomic)<<" dealloc: "<<(done_dealloc - done_destroying)<<" total: "<<(done_dealloc - start_run_helper)<<std::endl;

  }while (!ALAR(all_done));
      
  std::cout<<"iterations: "<<iter_num<<" logs_destroyed: "<<total_le_destroyed<<std::endl;

}

void *helper()
{
#ifdef _DISABLE_HELPER
    return 0;
#endif

#ifdef _PARALLEL_HELPER
    parallel_helper();
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
    
#ifdef _PROFILE_HT
    uint64_t start_graph_build, end_graph_build, total_graph_build = 0;
    uint64_t start_graph_resolve, end_graph_resolve, total_graph_resolve = 0;
    uint64_t start_graph_prune, end_graph_prune, total_graph_prune = 0;
#endif 
    
    do
    {
        uint64_t start_run, done_graph, done_resolve, done_prune = 0;   
        start_run = atlas_rdtsc();
        ++iter_num;

        LogStructure * lsp = 0;

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

        // Can't assert since a spurious wakeup may have happened
        if (!lsp) continue;

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
                if (ocs_count > HELPER_OCS_ANALYSIS_LIMIT) break;
                
                if (ALAR(all_done))
                {
                    is_parent_done = true;
                    break;
                }

                OcsMarker *current_ocs = CreateOcsMarker(current_le);
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
        done_graph = atlas_rdtsc(); 
        
        if (is_parent_done) break;

        UtilTrace4(helper_trace_file,
                  "Initial graph (%ld nodes): Iteration # %d\n",
                  num_vertices(dg), iter_num);
        GraphTrace(dg);
        
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
        done_graph = atlas_rdtsc();      
        
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
        done_prune = atlas_rdtsc();
        std::cout<<"graph build: "<<(done_graph-start_run)<<" resolve: "<<(done_resolve-done_graph)<<" prune: "<<(done_prune-done_resolve)<<" total: "<<(done_prune - start_run)<<std::endl;

    }while (!ALAR(all_done));

    // We want to return as quickly as possible (even if that means that
    // some memory is leaked) because the odds are that we are
    // exiting and so it does not matter.
//    DestroyOcses(&ocs_vec); // in case we broke out of the loop above
    Finalize_helper();

#ifdef _PROFILE_HT
    fprintf(stderr, "[HELPER] Total graph creation cycles = %ld\n",
            total_graph_build);
    fprintf(stderr, "[HELPER] Total graph resolve cycles  = %ld\n",
            total_graph_resolve);
    fprintf(stderr, "[HELPER] Total graph prune cycles    = %ld\n",
            total_graph_prune);
    extern uint64_t total_log_destroy_cycles;
    fprintf(stderr, "[HELPER] Total log destroy cycles    = %ld\n",
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
    fprintf(stderr, "[HELPER] Iteration count is %d\n", iter_num);
    fprintf(stderr, "[HELPER] ");
    NVM_PrintNumFlushes();
#endif    
    fprintf(stderr, "[HELPER] Removed %ld log entries\n", removed_log_count);
    printf("iterations: %d removed log count: %d\n", iter_num, removed_log_count);
    return 0;
}

