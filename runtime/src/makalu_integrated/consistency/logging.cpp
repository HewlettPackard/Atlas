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
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <set>

#include "atlas_api.h"
#include "atlas_alloc.h"
#include "atlas_alloc_priv.h"
#include "region_internal.h"
#include "log_structure.h"
#include "util.h"
#include "log_alloc.h"
#include "defer_free.h"

// TODO
#define WK_THR 100
#define DEFAULT_CACHE_LINE_SIZE 64
#define DEFAULT_CACHE_LINE_MASK (0xffffffffffffffff - CACHE_LINE_SIZE + 1)

// Globals accessed only by the main thread

// Ideally, the following should go into the alloc module, it does not
// belong here. TODO.
int mmapped_fd;
void * non_mutated_addr;
uint32_t nvm_logs_id = -1;
uint32_t cache_line_size;
uint64_t cache_line_mask;

// Shared globals

// Ideally, the following should go into the alloc module, it does not
// belong here. TODO.
void *mem_addr;

intptr_t *volatile OwnerTab[TAB_SIZE];
intptr_t *volatile LockCountTab[TAB_SIZE];

pthread_mutex_t LockTab[TAB_SIZE];

// acquire this lock when printing something to stderr
pthread_mutex_t print_lock;

#ifdef NVM_STATS
// acquire this lock when updating shared critical section related stats
pthread_mutex_t atlas_stats_cs_lock;
// acquire this lock when updating shared write-related stats
pthread_mutex_t atlas_stats_wr_lock;
// acquire this lock when updating log memory usage stats
pthread_mutex_t atlas_stats_log_mem_use_lock;
#endif

// This is the helper thread that is created at initialization time
// and joined at finalization time. It is manipulated by the main thread
// alone avoiding a race.
pthread_t helper_thread;

LogStructure * volatile * log_structure_header_p = 0;

CbListNode<LogEntry> * volatile cb_log_list = 0;

// a running counter of threads that is used to assign a unique id
// to a given thread
intptr_t thread_counter = 0;

// indicator whether the user threads are done
volatile AO_t all_done = 0;

pthread_cond_t helper_cond;
pthread_mutex_t helper_lock;

#ifdef NVM_STATS
// Warning: we acquire locks while updating these. So make sure these
// don't get enabled in performance-critical runs.
// We compute the total number of critical sections as the number of
// lock acquires. 
uint64_t total_cs_count = 0;

// On a lock acquire, if the total number of locks held is greater than 1,
// we count that as a nested critical section.
uint64_t nested_cs_count = 0;

// Total number of writes logged (memset/memcpy, etc. counted as 1)
uint64_t total_logged_str_count = 0;

// Total number of writes encountered within critical sections
uint64_t cs_str_logged_count = 0;

// Total number of writes not logged
uint64_t total_strs_not_logged = 0;

uint64_t log_opt_failed_count = 0;

uint64_t strs_in_cs_not_logged = 0;

uint64_t log_mem_use = 0;
#endif

// Here are the thread-local global variables

// Log tracker pointing to the last log entry of this thread
__thread LogEntry * tl_last_log_entry = 0;

// Count of locks acquired. A non-zero value indicates that execution is
// within a Top-level Critical Section (TCS).
// TODO: Is this the right way to check for a TCS? The POSIX man page
// says that if an unlock is attempted on an already-released lock,
// undefined behavior results. So it appears that we can use this logic
// to support this simple detection of TCS.
__thread intptr_t num_acq_locks = 0;

// Track start of TCS logically. This will either be the first log entry
// after acquiring the first lock in a TCS or this will be the first log
// entry after releasing the last lock in a TCS (even though this log
// entry is because of a store that occurs without holding a lock).
__thread bool is_logical_tcs_start = true;

// Used to signal the helper thread indicating that there may be work to do
__thread uint32_t log_count = 0;

#if (defined(_FLUSH_LOCAL_COMMIT) || defined(_FLUSH_GLOBAL_COMMIT)) && \
    !defined(DISABLE_FLUSHES) && !defined(_DISABLE_DATA_FLUSH)
// Used to track the set used for cache line flushing
__thread SetOfInts *tl_flush_ptr = 0;
#endif

// Used to track unique address/size pair within a consistent section
__thread SetOfPairs *tl_uniq_loc = 0;

#ifdef NVM_STATS
__thread uint64_t num_flushes;
#endif

__thread MapOfLockInfo *tl_undo_locks = 0;
__thread bool should_log_non_cs_stmt = true;
__thread bool is_first_non_cs_stmt = true;

__thread CbLog<LogEntry> *tl_cb_log = 0;

__thread intptr_t LogFlushTab[FLUSH_TAB_SIZE];
__thread intptr_t DataFlushTab[FLUSH_TAB_SIZE];

// Every time the information in a log entry is over-written (either
// because it is newly created or because it is repurposed), a
// monotonically increasing generation number is assigned to it.
__thread uint64_t tl_gen_number = 0;
__thread uint64_t tl_counter = 0;

#if defined(_NVM_TRACE) || defined(_NVM_VERBOSE_TRACE)
__thread FILE *tl_trace_file = 0;
__thread uint64_t tl_log_mem = 0;
__thread uint64_t tl_log_alloc_count = 0;
#endif

#ifdef NVM_STATS
void IncrementNotLoggedStats()
{
    int status = pthread_mutex_lock(&atlas_stats_wr_lock);
    assert(!status);
    ++total_strs_not_logged;
    status = pthread_mutex_unlock(&atlas_stats_wr_lock);
    assert(!status);
}
void IncrementLogOptFailedStats()
{
    int status = pthread_mutex_lock(&atlas_stats_wr_lock);
    assert(!status);
    ++log_opt_failed_count;
    status = pthread_mutex_unlock(&atlas_stats_wr_lock);
    assert(!status);
}
void IncrementNotLoggedInCSStats()
{
    int status = pthread_mutex_lock(&atlas_stats_wr_lock);
    assert(!status);
    ++strs_in_cs_not_logged;
    status = pthread_mutex_unlock(&atlas_stats_wr_lock);
    assert(!status);
}
void IncrementLogMemUseStats(size_t sz)
{
    int status = pthread_mutex_lock(&atlas_stats_log_mem_use_lock);
    assert(!status);
    log_mem_use += sz;
    status = pthread_mutex_unlock(&atlas_stats_log_mem_use_lock);
    assert(!status);
}
#endif

// This function is called when the caller needs a new circular buffer
template<class T>
CbLog<T> *GetNewCb(uint32_t size, uint32_t rid, CbLog<T> **log_p,
                   CbListNode<T> * volatile *cb_list_p)
{
    // The data structures used in managing the circular buffer need
    // not be in persistent memory. After program termination, the
    // circular buffer metadata is not used. During normal termination,
    // the log file is deleted automatically releasing all the
    // resources for the remaining log entries. In the case of
    // abnormal program termination, the recovery phase never needs
    // these management data structures and the log file is again
    // deleted after successful recovery. The helper thread would free
    // a circular buffer if it is empty but that does not require
    // persistence.

    if (*log_p) ASRW_int((*log_p)->isFilled_, true);
    // first time around for this thread
    else InitializeThreadLocalTraceFile(&tl_trace_file);
    
    // Search through the list of cbl_nodes looking for one that is
    // available and is owned by this thread. If found, set isAvailable
    // to false, isfilled to false, and return it.
    CbListNode<T> *curr_search = (CbListNode<T>*)ALAR(*cb_list_p);
    while (curr_search)
    {
        if ((uint32_t)ALAR_int(curr_search->isAvailable_) &&
            pthread_equal(curr_search->Tid_, pthread_self()))
        {
            assert(curr_search->Cb_);
            assert((uint32_t)ALAR_int(curr_search->Cb_->isFilled_));

            *log_p = curr_search->Cb_;
            
            ASRW_int(curr_search->Cb_->isFilled_, false);
            curr_search->Cb_->Start_ = 0;
            curr_search->Cb_->End_ = 0;

            ASRW_int(curr_search->isAvailable_, false);

#if defined(_NVM_TRACE) || defined(_NVM_VERBOSE_TRACE)
            UtilTrace2(tl_trace_file, "Reusing circular buffer for logging\n");
#endif            
            return curr_search->Cb_;
        }
        curr_search = curr_search->Next_;
    }

    CbLog<T> *cb = (CbLog<T>*)malloc(sizeof(CbLog<T>));
    cb->Size_ = size+1; // taking into account the dummy element
    ASRW_int(cb->isFilled_, false);
    cb->Start_ = 0;
    cb->End_ = 0;
    cb->LogArray_ = (T*)nvm_alloc_cache_line_aligned(
        cb->Size_*sizeof(T), nvm_logs_id);

#if defined(_NVM_TRACE) || defined(_NVM_VERBOSE_TRACE)    
    tl_log_mem += cb->Size_*sizeof(T);
    UtilTrace3(tl_trace_file, "Log mem usage is %ld\n", tl_log_mem);
#endif    

#ifdef NVM_STATS
    IncrementLogMemUseStats(cb->Size_*sizeof(T));
#endif
    
    *log_p = cb;
    
    // New cbl_node must be inserted at the head
    CbListNode<T> *cbl_node = (CbListNode<T>*)malloc(sizeof(CbListNode<T>));
    cbl_node->Cb_ = cb;
    cbl_node->StartAddr_ = (char*)(cb->LogArray_);
    cbl_node->EndAddr_ = cbl_node->StartAddr_ + cb->Size_*sizeof(T) - 1;
    cbl_node->Tid_ = pthread_self();
    ASRW_int(cbl_node->isAvailable_, false);

    CbListNode<T> *oldval, *first_p;
    do
    {
        first_p = (CbListNode<T>*)ALAR(*cb_list_p);
        cbl_node->Next_ = first_p;
        oldval = (CbListNode<T>*)CAS(cb_list_p, first_p, cbl_node);
    }while (oldval != first_p);

    return cb;
}

// A user thread is the only entity that adds a CB slot
// The helper thread is the only entity that deletes a CB slot
template<class T>
inline T *GetNewSlot(uint32_t rid, CbLog<T> **log_p,
                     CbListNode<T> * volatile *cb_list_p)
{
    if (!*log_p || isCbFull(*log_p))
        GetNewCb<T>(DEFAULT_CB_SIZE, rid, log_p, cb_list_p);

    ++ tl_counter;
    if (tl_counter % DEFAULT_CB_SIZE == 0) ++ tl_gen_number;
    
    T *r = &((*log_p)->LogArray_[(*log_p)->End_]);
    ASRW((*log_p)->End_, (ALAR((*log_p)->End_)+1) & ((*log_p)->Size_-1));

#if defined(_NVM_VERBOSE_TRACE)
    tl_log_alloc_count ++;
    if (!((tl_log_alloc_count+1) % TRACE_GRANULE))
        UtilVerboseTrace3(tl_trace_file, "Log entry count = %ld\n",
                          tl_log_alloc_count);
#endif

    return r;
}

void AsyncLogFlush(void *p)
{
    // Since this is a log entry, we don't need to check whether it is
    // persistent or not. It must be persistent.
    intptr_t *entry = LogFlushTab +
        (((intptr_t)p >> FLUSH_SHIFT) & FLUSH_TAB_MASK);
    intptr_t cache_line = (intptr_t)p & cache_line_mask;

    if (*entry != cache_line)
    {
        if (*entry)
        {
            AO_nop_full();
            NVM_CLFLUSH(*entry);
        }
        *entry = cache_line;
    }
}

void SyncLogFlush()
{
    int i;
    AO_nop_full();
    for (i=0; i<FLUSH_TAB_SIZE; ++i)
    {
        intptr_t *entry = LogFlushTab + i;
        if (*entry)
        {
            NVM_CLFLUSH(*entry);
            *entry = 0;
        }
    }
    AO_nop_full();
}

#if defined(_USE_TABLE_FLUSH)
void AsyncDataFlush(void *p)
{
    if (!NVM_IsInOpenPR(p, 1)) return;

    intptr_t *entry = DataFlushTab +
        (((intptr_t)p >> FLUSH_SHIFT) & FLUSH_TAB_MASK);
    intptr_t cache_line = (intptr_t)p & cache_line_mask;

    if (*entry != cache_line)
    {
        if (*entry)
        {
            AO_nop_full();
            NVM_CLFLUSH(*entry);
        }
        *entry = cache_line;
    }
}

void AsyncMemOpDataFlush(void *dst, size_t sz)
{
    if (!NVM_IsInOpenPR(dst, 1)) return;

    if (sz <= 0) return;
    
    char *last_addr = (char*)dst + sz - 1;
    char *cacheline_addr =
        (char*)(((uint64_t)dst) & cache_line_mask);
    char *last_cacheline_addr =
        (char*)(((uint64_t)last_addr) & cache_line_mask);

    intptr_t *entry;
    AO_nop_full();
    do 
    {
        entry = DataFlushTab +
            (((intptr_t)cacheline_addr >> FLUSH_SHIFT) & FLUSH_TAB_MASK);
        if (*entry != (intptr_t)cacheline_addr)
        {
            if (*entry) NVM_CLFLUSH(*entry);
            *entry = (intptr_t)cacheline_addr;
        }
        cacheline_addr += cache_line_size;
    }while (cacheline_addr < last_cacheline_addr+1);
}

void SyncDataFlush()
{
    int i;
    AO_nop_full();
    for (i=0; i<FLUSH_TAB_SIZE; ++i)
    {
        intptr_t *entry = DataFlushTab + i;
        if (*entry)
        {
            NVM_CLFLUSH(*entry);
            *entry = 0;
        }
    }
    AO_nop_full();
}
#else
void AsyncDataFlush(void *p) {assert(0);}
void AsyncMemOpDataFlush(void *dst, size_t sz) {assert(0);}
void SyncDataFlush() {assert(0);}
#endif

inline void flush_log_uncond(void *p)
{
#if (!defined(DISABLE_FLUSHES) && !defined(_DISABLE_LOG_FLUSH))
#if defined(_LOG_FLUSH_OPT)
    // TODO: this needs more work. It is incomplete.
    AsyncLogFlush(p);
#else
    NVM_FLUSH(p);
#endif
#endif
}

#if defined(_USE_MOVNT)
inline void mov_nt(LogEntry *le, void *addr, size_t sz, LE_TYPE le_type)
{
    LogEntry le_nt;

    le_nt.Addr_ = addr;
    if (isStartSection(le_type) || isEndSection(le_type))
        le_nt.ValueOrPtr_ = 0;
    else if (isStr(le_type))
        memcpy((void*)&(le_nt.ValueOrPtr_), addr, sz/8);
    else if (isMemop(le_type))
    {
        le_nt.ValueOrPtr_ = (intptr_t)(char*)nvm_alloc(sz, nvm_logs_id);
        assert(le_nt.ValueOrPtr_);
        // TODO mov_nt here
        memcpy((void*)le_nt.ValueOrPtr_, addr, sz);
    }
    else assert(le_type == LE_dummy);
    
    le_nt.Next_ = 0;
    le_nt.Size_ = sz;
    le_nt.Type_ = le_type;

    long long unsigned int *from = (long long unsigned int*)&le_nt;
    long long unsigned int *to = (long long unsigned int*)le;
    uint32_t i;
    assert(sizeof(le_nt) % 8 == 0);
    for (i=0; i < sizeof(le_nt)/8; ++i)
        __builtin_ia32_movntq(to+i, *(from+i));
#if !defined(_NO_SFENCE)    
    __builtin_ia32_sfence();
#endif    
}
#endif

inline LogEntry *CreateLogEntry(void *lock_address, LE_TYPE le_type)
{
    assert(le_type == LE_acquire || le_type == LE_release ||
           le_type == LE_begin_durable || le_type == LE_end_durable ||
           le_type == LE_rwlock_rdlock || le_type == LE_rwlock_wrlock ||
           le_type == LE_rwlock_unlock);
    
    // First create the log entry
#if defined(_LOG_WITH_MALLOC)
    LogEntry *le = (LogEntry *) malloc(sizeof(LogEntry));
#elif defined(_LOG_WITH_NVM_ALLOC)
    LogEntry *le = (LogEntry *) nvm_alloc(sizeof(LogEntry), nvm_logs_id);
#else
    LogEntry *le = GetNewSlot<LogEntry>(nvm_logs_id, &tl_cb_log, &cb_log_list);
#endif    
    assert(le);

#if defined(_USE_MOVNT)
    mov_nt(le, lock_address, 0, le_type);
#else
    le->Addr_ = lock_address;
    le->ValueOrPtr_ = 0;
    le->Next_ = 0;

    // Note that the generation number is currently set for release
    // log entries only. We don't yet know how to get rdwr unlocks to work
    // for this case, so leave that alone.
    if (le_type == LE_release) le->Size_ = tl_gen_number;
    else le->Size_ = sizeof(uintptr_t); // ptr-sized but won't matter
    le->Type_ = le_type;

#if !defined(_LOG_WITH_NVM_ALLOC) && !defined(_LOG_WITH_MALLOC)    
    assert(!isOnDifferentCacheLine(le, LAST_LOG_ELEM(le)));
#endif
#endif
    
    return le;
}

inline LogEntry * CreateStrLogEntry(void * addr, size_t size_in_bits)
{
    assert(size_in_bits <= 8*sizeof(uintptr_t));
    assert(!(size_in_bits % 8));

#if defined(_LOG_WITH_MALLOC)
    LogEntry *le = (LogEntry *) malloc(sizeof(LogEntry));
#elif defined(_LOG_WITH_NVM_ALLOC)
    LogEntry *le = (LogEntry *) nvm_alloc(sizeof(LogEntry), nvm_logs_id);
#else
    LogEntry *le = GetNewSlot<LogEntry>(nvm_logs_id, &tl_cb_log, &cb_log_list);
#endif

    assert(le);

#if defined(_USE_MOVNT)
    mov_nt(le, addr, size_in_bits, LE_str);
#else

    le->Addr_ = addr;
    memcpy((void*)&(le->ValueOrPtr_), addr, size_in_bits/8);
    le->Next_ = 0;
    le->Size_ = size_in_bits;
    le->Type_ = LE_str;

#if !defined(_LOG_WITH_NVM_ALLOC) && !defined(_LOG_WITH_MALLOC)
    assert(!isOnDifferentCacheLine(le, LAST_LOG_ELEM(le)));
#endif    
#endif
    return le;
}

inline LogEntry * CreateLogEntry(void * addr, size_t sz, LE_TYPE le_type)
{
    assert(le_type == LE_memset || le_type == LE_memcpy ||
           le_type == LE_memmove);

#if defined(_LOG_WITH_MALLOC)
    LogEntry *le = (LogEntry *) malloc(sizeof(LogEntry));
#elif defined(_LOG_WITH_NVM_ALLOC)
    LogEntry *le = (LogEntry *) nvm_alloc(sizeof(LogEntry), nvm_logs_id);
#else
    LogEntry *le = GetNewSlot<LogEntry>(nvm_logs_id, &tl_cb_log, &cb_log_list);
#endif    
    assert(le);

#if defined(_USE_MOVNT)
    mov_nt(le, addr, sz, le_type);
#else    
    le->Addr_ = (intptr_t *)addr;

    // For a mem*, ValueOrPtr_ is a pointer to the sequence of old
    // vals
#if defined(_LOG_WITH_MALLOC)    
    le->ValueOrPtr_ = (intptr_t)(char *) malloc(sz);
//#elif defined(_LOG_WITH_NVM_ALLOC)    // no special casing
#else
    le->ValueOrPtr_ = (intptr_t)(char *) nvm_alloc(sz, nvm_logs_id);
#endif
    assert(le->ValueOrPtr_);

    memcpy((void*)le->ValueOrPtr_, addr, sz);

    NVM_PSYNC((void*)le->ValueOrPtr_, sz);
    
    le->Next_ = 0;
    le->Size_ = sz;
    le->Type_ = le_type;

#if !defined(_LOG_WITH_NVM_ALLOC) && !defined(_LOG_WITH_MALLOC)
    assert(!isOnDifferentCacheLine(le, LAST_LOG_ELEM(le)));
#endif    
#endif    
    return le;
}

inline LogEntry *CreateDummyLogEntry()
{
#if defined(_LOG_WITH_MALLOC)
    LogEntry *le = (LogEntry *) malloc(sizeof(LogEntry));
#elif defined(_LOG_WITH_NVM_ALLOC)
    LogEntry *le = (LogEntry *) nvm_alloc(sizeof(LogEntry), nvm_logs_id);
#else
    LogEntry *le = GetNewSlot<LogEntry>(nvm_logs_id, &tl_cb_log, &cb_log_list);
#endif    
    assert(le);

#if defined(_USE_MOVNT)
    mov_nt(le, 0, 0, LE_dummy);
#else    
    memset(le, 0, sizeof(LogEntry));
    le->Type_ = LE_dummy;
#endif    
    return le;
}

void PublishLogEntry(LogEntry *le)
{
    // if tl_last_log_entry is null, it means that this is the first
    // log entry created in this thread. In that case, allocate space
    // for the thread-specific header, set a pointer to this log entry,
    // and insert it into the list of headers.

    // TODO where to insert synclogflush here?
    if (!tl_last_log_entry) 
    {

   
        LogStructure *ls =
            (LogStructure*) nvm_alloc(sizeof(LogStructure), nvm_logs_id);
        assert(ls);
        
        SetThreadContext((void*)ls, InitThreadContext());

        flush_log_uncond(le);

        ls->Le_ = le;
        
        LogStructure *tmp, *oldval;
        do
        {
            // Ensure that data modified by read-modify-write
            // instructions reach NVRAM before being read
            flush_log_uncond((void*)log_structure_header_p);

            tmp = (LogStructure *) ALAR(*log_structure_header_p);

            ls->Next_ = tmp;

            // 16-byte alignment guarantees that an element of type
            // LogStructure is on the same cache line
            flush_log_uncond(&ls->Next_);

            oldval = (LogStructure *) CAS(log_structure_header_p, tmp, ls);
        }while (tmp != oldval);
        flush_log_uncond((void*)log_structure_header_p);
    }
    else
    {
        // "le" will be attached to the log structure here. This involves
        // resetting the Next_ field of tl_last_log_entry to "le".

        // In the default mode:
        // If "le" is the first entry in its cache line, flush "le",
        // reset the Next_ field and flush Next_ (2 cache line flushes).
        // If "le" is not the first entry in its cache line, assert
        // that tl_last_log_entry is in the same cache line, set
        // Next_ and flush this cache line (1 cache line flush).
#if defined(_USE_MOVNT)
        long long unsigned int *from = (long long unsigned int*)&le;
        __builtin_ia32_movntq(
            (long long unsigned int*)&tl_last_log_entry->Next_, *from);
#if !defined(_NO_SFENCE)            
        __builtin_ia32_sfence();
#endif        
#else            
        if (isCacheLineAligned(le))
        {
            flush_log_uncond(le);
            ASRW(tl_last_log_entry->Next_, le);
            flush_log_uncond((void*)&tl_last_log_entry->Next_);
        }
        else
        {
#if !defined(_LOG_WITH_NVM_ALLOC) && !defined(_LOG_WITH_MALLOC)
            assert(!isOnDifferentCacheLine(
                       le, (void*)&tl_last_log_entry->Next_));
            ASRW(tl_last_log_entry->Next_, le);
            flush_log_uncond(le);
#else
            if (isOnDifferentCacheLine(le, (void*)&tl_last_log_entry->Next_))
            {
                flush_log_uncond(le);
                ASRW(tl_last_log_entry->Next_, le);
                flush_log_uncond((void*)&tl_last_log_entry->Next_);
            }
            else
            {
                ASRW(tl_last_log_entry->Next_, le);
                flush_log_uncond(le);
            }
#endif            
        }
#endif        
    }
}

// The following is called by the helper thread alone (today).
LogStructure *CreateLogStructure(LogEntry *le)
{
    LogStructure *lsp = (LogStructure*)
        nvm_alloc(sizeof(LogStructure), nvm_logs_id);
    assert(lsp);

    // The following will fail if called by anything other than the helper
    UtilVerboseTrace3(helper_trace_file,
                      "[HELPER] Created header node %p\n", lsp);
    
    lsp->Le_ = le;
    lsp->Next_ = 0;
    // Because of 16-byte alignment of all allocated memory on NVRAM, the above
    // two fields will always be on the same cache line
    flush_log_uncond(lsp);
    return lsp;
}

inline OwnerInfo * GetHeader(void * lock_address)
{
    // Index into the OwnerTab
    intptr_t * volatile * table_ptr = O_ENTRY(lock_address);
    return  (OwnerInfo * volatile) ALAR(*table_ptr);
}

inline LockCount *GetLockCountHeader(void *lock_address)
{
    intptr_t * volatile * entry = LOCK_COUNT_TABLE_ENTRY(lock_address);
    return (LockCount * volatile) ALAR(*entry);
}

inline OwnerInfo * volatile * GetPointerToHeader(void * lock_address)
{
    // Index into the OwnerTab
    intptr_t * volatile * table_ptr = O_ENTRY(lock_address);
    return  (OwnerInfo * volatile *)table_ptr;
}

inline LockCount * volatile * GetPointerToLockCountHeader(void *lock_address)
{
    intptr_t * volatile * entry = LOCK_COUNT_TABLE_ENTRY(lock_address);
    return (LockCount * volatile *)entry;
}

inline OwnerInfo *FindOwnerOfLock(void *lock_address)
{
    OwnerInfo *oip = GetHeader(lock_address);
    while (oip)
    {
        ImmutableInfo *ii = (ImmutableInfo*)ALAR(oip->II_);
        assert(ii);
        if (ii->isDeleted_) 
        {
          oip = oip->Next_;
          continue;
        }
        LogEntry *le = ii->LogAddr_;
        assert(le);
        assert(isRelease(le) || isRWLockUnlock(le));
        if (le->Addr_ == lock_address) return oip;
        oip = oip->Next_;
    }
    return 0;
}

inline OwnerInfo *FindOwnerOfLogEntry(LogEntry *candidate_le)
{
    OwnerInfo *oip = GetHeader(candidate_le->Addr_);
    while (oip)
    {
        ImmutableInfo *ii = (ImmutableInfo*)ALAR(oip->II_);
        assert(ii);
        if (ii->isDeleted_) 
        {
          oip = oip->Next_;
          continue;
        }
        LogEntry *le = ii->LogAddr_;
        assert(le);
        assert(isRelease(le) || isRWLockUnlock(le));
        if (le->Addr_ != candidate_le->Addr_)
        {
            oip = oip->Next_;
            continue;
        }
        else if (le == candidate_le) return oip;
        else return 0;
    }
    return 0;
}

inline LockCount *FindLockCount(void *lock_address)
{
    LockCount *lcp = GetLockCountHeader(lock_address);
    while (lcp)
    {
        if (lcp->LockAddr_ == lock_address) return lcp;
        lcp = lcp->Next_;
    }
    return 0;
}

inline ImmutableInfo *CreateNewImmutableInfo(
    LogEntry *le, const MapOfLockInfo & undo_locks, bool is_deleted)
{
    ImmutableInfo *ii = (ImmutableInfo*) malloc(sizeof(ImmutableInfo));
    ii->LogAddr_ = le;
    ii->LockInfoPtr_ = new MapOfLockInfo(undo_locks);
    ii->isDeleted_ = is_deleted;

    return ii;
}

inline OwnerInfo *CreateNewOwnerInfo(
    LogEntry *le, const MapOfLockInfo & undo_locks)
{
    ImmutableInfo *ii = CreateNewImmutableInfo(le, undo_locks, false);
    OwnerInfo *owner_entry = (OwnerInfo*) malloc(sizeof(OwnerInfo));
    owner_entry->II_ = ii;
    owner_entry->Next_ = NULL;
    
    return owner_entry;
}

inline LockCount *CreateNewLockCount(void *lock_address, uint64_t count)
{
    LockCount *lc = (LockCount*) malloc(sizeof(LockCount));
    lc->LockAddr_ = lock_address;
    lc->Count_ = count;
    lc->Next_ = 0;
    return lc;
}

// The routine AddLogToOwnerTable is called by a release operation.
// When a lock is released the first time (regardless of the thread
// performing the release), no corresponding entry exists. For all
// subsequent releases of the same lock, an entry may be found. The
// find operation can proceed without any locking underneath.

// The following routines manipulate OwnerTab, essentially a hash
// table, mapping a lock address to the log entry corresponding to the
// last release operation of that lock.

// Operations supported: insert/update/add, find, and delete.

// Insert/update/add: This is called by the lock release entry point. If
// no valid entry exists for the provided lock, a new entry is created and
// added to the head of the list for the appropriate bucket. If a valid
// entry exists for the lock address, we redirect the information using
// a copy-on-write technique. A new instance of ImmutableInfo is created,
// populated appropriately and the pointer to the old instance is updated.
// We need RMW atomic operations for the update since there may be multiple
// writers.

// Find: Since multiple locks may hash into the same bucket, the find
// operation must check the lock address with the log entries obtained from
// a bucket. The find operation atomically reads off the list of entries from
// a bucket and can safely traverse through the list in a non-blocking
// manner.

// Delete: The helper thread deletes log entries and if a deleted log entry
// is found in OwnerTab, that entry is marked deleted. A deleted log entry
// will not be reused by insert and find operations. Periodically, the
// helper thread checks whether, for a given bucket, a bunch of deleted
// entries reside at the end of the corresponding list. If so, the list
// can be truncated atomically. This *cleaning* of the list prevents reuse
// of a deleted entry.
void AddLogToOwnerTable(LogEntry *le, const MapOfLockInfo & undo_locks)
{
    assert(isRelease(le) || isRWLockUnlock(le));

    bool done = false;
    while (!done)
    {
        OwnerInfo *oi = FindOwnerOfLock(le->Addr_);

        // TODO: Does reusing a deleted entry help in any way?
        if (oi)
        {
            ImmutableInfo *new_ii =
                CreateNewImmutableInfo(le, undo_locks, false);
            ImmutableInfo *old_ii, *curr_ii;
            do 
            {
                curr_ii = (ImmutableInfo*)ALAR(oi->II_);

                // Even if it was found earlier, it may have been deleted
                // by the helper thread since then. So need to find again.
                oi = FindOwnerOfLock(le->Addr_);
                if (!oi) break;
                
                old_ii = (ImmutableInfo*)CAS(&oi->II_, curr_ii, new_ii);
            }while (old_ii != curr_ii);

            // It is ok to delete the old map since any read/write of the
            // map can be done only while holding a lock
            if (oi)
            {
                assert(!oi->II_->isDeleted_);
                done = true;
                delete old_ii->LockInfoPtr_;
            }
        }
        else
        {
            // TODO
            // This is a bug in a certain way for rwlock. For rwlock, we
            // need to call FindOwnerInfo here again (or within a CAS-loop).
            // Otherwise, two entries for the same lock within the hash table
            // may exist which may or may not create problems.
            OwnerInfo *new_entry = CreateNewOwnerInfo(le, undo_locks);
            OwnerInfo *oldval, *first_oip;
            do
            {
                first_oip = GetHeader(le->Addr_);
                // Insertion at the head
                new_entry->Next_ = first_oip;
                oldval = (OwnerInfo*)CAS(GetPointerToHeader(le->Addr_),
                                         first_oip, new_entry);
            }while (oldval != first_oip);
            done = true;
        }
    }
}

void DeleteOwnerInfo(LogEntry *le)
{
    assert(isRelease(le) || isRWLockUnlock(le));

    // An owner info may not be found since the one that existed before
    // for this log entry may have been overwritten by one of the user
    // threads.
    OwnerInfo *oi = FindOwnerOfLogEntry(le);
    if (oi)
    {
        ImmutableInfo *curr_ii = (ImmutableInfo*)ALAR(oi->II_);
        ImmutableInfo *new_ii = CreateNewImmutableInfo(
            curr_ii->LogAddr_, *curr_ii->LockInfoPtr_, true);
        ImmutableInfo *old_ii = (ImmutableInfo*)CAS(&oi->II_, curr_ii, new_ii);
        if (old_ii == curr_ii) // deletion succeeded
            delete curr_ii->LockInfoPtr_;
    }
}

void AddLockCount(void *lock_address, uint64_t count)
{
    LockCount *lc = FindLockCount(lock_address);
    if (lc) ASRW(lc->Count_, count);
    else
    {
        LockCount *new_entry = CreateNewLockCount(lock_address, count);
        LockCount *oldval, *first_lcp;
        do
        {
            first_lcp = GetLockCountHeader(lock_address);
            new_entry->Next_ = first_lcp;
            oldval = (LockCount*)CAS(GetPointerToLockCountHeader(lock_address),
                                     first_lcp, new_entry);
        }while (oldval != first_lcp);
    }
}

inline void SignalHelper()
{
    ++log_count;
    if (log_count == WK_THR)
    {
        pthread_cond_signal(&helper_cond);
        log_count = 0;
    }
}

// Main tasks done just after a lock is acquired:
// Create a new log entry for the acquire
// Get the address of the log entry for the corresponding release, if any
// Get/Set the address of the last log entry in this thread (in program order)

void FinishAcquire(void *lock_address, LogEntry *le)
{
    assert(num_acq_locks >= 0);

    ++num_acq_locks;

#ifdef NVM_STATS
    int status;
    status = pthread_mutex_lock(&atlas_stats_cs_lock);
    assert(!status);
    
    ++total_cs_count;
    if (num_acq_locks > 1) ++nested_cs_count;

    status = pthread_mutex_unlock(&atlas_stats_cs_lock);
    assert(!status);
#endif

#ifndef _NO_NEST    
    if (lock_address)
    {
        LockCount *lcp = FindLockCount(lock_address);
        uint64_t lock_count;
        if (lcp) lock_count = lcp->Count_;
        else
        {
            lock_count = 0;
            AddLockCount(lock_address, 0);
        }
        if (!tl_undo_locks) tl_undo_locks = new MapOfLockInfo;
        (*tl_undo_locks)[lock_address] = lock_count;

        // Find the log entry corresponding to the release of this lock
        // If null, the inter-thread pointer is left as is
        OwnerInfo *oi = FindOwnerOfLock(lock_address);
        if (oi)
        {
            ImmutableInfo *ii = (ImmutableInfo*)ALAR(oi->II_);
            le->ValueOrPtr_ = (intptr_t)(ii->LogAddr_);

            // Set the generation number
            // Note that the release log entry _must_ exist for the
            // assignment below to work.
            assert(isRelease((LogEntry*)(le->ValueOrPtr_)) ||
                   isRWLockUnlock((LogEntry*)(le->ValueOrPtr_)));
            le->Size_ = ((LogEntry*)(le->ValueOrPtr_))->Size_;
            
            MapOfLockInfo *moli = ii->LockInfoPtr_;
            if (moli) (*tl_undo_locks).insert((*moli).begin(), (*moli).end());
        }
    }
#endif
    
    PublishLogEntry(le);

    tl_last_log_entry = le;
}

// TODO: trylock is not handled. It is unclear how to handle it in the
// general case.
void nvm_acquire(void * lock_address)
{
    // First create a log node
    LogEntry * le = CreateLogEntry(lock_address, LE_acquire);
    assert(le);

    FinishAcquire(lock_address, le);
}

// We track whether a rwlock is acquired in read or write mode. But this
// information is not used today. It can potentially be used for optimizing
// away hb-relations between a rwlock_unlock (rd mode) and a rwlock_rwlock
// but that would require finding the correct rwlock_unlock (wr mode) log
// entry for the hb-relation. That appears to be more expensive than just
// maintaining hb-relations even within read mode release/acquires. The
// current design is correct but risks losing some consistent states.
void nvm_rwlock_rdlock(void * lock_address)
{
    LogEntry * le = CreateLogEntry(lock_address, LE_rwlock_rdlock);
    assert(le);

    FinishAcquire(lock_address, le);
}

void nvm_rwlock_wrlock(void * lock_address)
{
    LogEntry * le = CreateLogEntry(lock_address, LE_rwlock_wrlock);
    assert(le);

    FinishAcquire(lock_address, le);
}

// Main tasks done just before a lock is released:
// Create a new log entry for the release
// Set this new log address in the owner table
// Get/Set the address of the last log entry for this thread
inline void FinishRelease(LogEntry *le, const MapOfLockInfo & undo_locks)
{
    if (num_acq_locks <= 0) PrintBackTrace();
        
    assert(num_acq_locks > 0);
    -- num_acq_locks;
    
    PublishLogEntry(le);

#ifndef _NO_NEST    
    AddLogToOwnerTable(le, undo_locks);
#endif    

    tl_last_log_entry = le;

    if (!num_acq_locks) MarkEndTCS(le);
}

inline uint64_t RemoveLock(void *lock_address)
{
    assert(tl_undo_locks);
    MapOfLockInfo::iterator ci = tl_undo_locks->find(lock_address);
    assert(ci != tl_undo_locks->end());
    uint64_t lock_count = ci->second;
    tl_undo_locks->erase(ci);
    return lock_count;
}

void nvm_release(void *lock_address)
{
    // TODO this should not be required.
    // Temporary workaround for memcached. multiple issues:
    // 1. need to handle trylock: currently handled manually.
    // 2. memcached appears to call slabs_rebalancer_resume before
    //    acquiring the corresponding lock. code changed.
    // 3. more unknown issues
    if (num_acq_locks <= 0) return;
    
    LogEntry *le = CreateLogEntry(lock_address, LE_release);
    assert(le);

#ifndef _NO_NEST    
    uint64_t lock_count = RemoveLock(lock_address);

    // clean up the thread-local table
    canElideLogging();
#endif
    
    FinishRelease(le, *tl_undo_locks);

#ifndef _NO_NEST    
    // The following must happen after publishing
    AddLockCount(lock_address, lock_count+1);
#endif    
    
    SignalHelper();
}

void nvm_rwlock_unlock(void * lock_address)
{
    LogEntry * le = CreateLogEntry(lock_address, LE_rwlock_unlock);
    assert(le);

    uint64_t lock_count = RemoveLock(lock_address);

    // clean up the thread-local table
    canElideLogging();
    
    FinishRelease(le, *tl_undo_locks);

    // The following must happen after publishing
    AddLockCount(lock_address, lock_count+1);
    
    SignalHelper();
}

inline void MarkEndTCS(LogEntry *le)
{
#ifdef _OPT_UNIQ_LOC
    if (tl_uniq_loc) tl_uniq_loc->clear();
#endif    

#if defined(_FLUSH_LOCAL_COMMIT) && !defined(DISABLE_FLUSHES) && \
    !defined(_DISABLE_DATA_FLUSH)
    FlushCacheLines();
#elif defined(_USE_TABLE_FLUSH)
    SyncDataFlush();
#endif    
    CreateDeferredFreeNode((void*) le); 
    is_first_non_cs_stmt = true;

    // Since this is the end of a failure-atomic section, create a
    // dummy log entry. A dummy log entry ensures that there is at least
    // one outstanding log entry even if all other log entries are pruned
    // out. Note that currently we are adding a dummy log entry for
    // every failure-atomic section. This can be costly. Static
    // analysis should be used to elide this dummy log entry if it can
    // prove that there is either a failure-atomic section or a store
    // instruction following this failure-atomic section.
    LogEntry *dummy_le = CreateDummyLogEntry();
    PublishLogEntry(dummy_le);
    tl_last_log_entry = dummy_le;
}
    
// This is just modeled as an increment in the number of locks held.
// No need to add a log entry
void nvm_begin_durable()
{
    LogEntry *le = CreateLogEntry(NULL, LE_begin_durable);
    assert(le);
    FinishAcquire(NULL, le);
}

// This is just modeled as a decrement in the number of locks held.
// No need to add a log entry
void nvm_end_durable()
{
    LogEntry *le = CreateLogEntry(NULL, LE_end_durable);
    assert(le);
    
    assert(num_acq_locks > 0);
    -- num_acq_locks;

    PublishLogEntry(le);
    tl_last_log_entry = le;

    //if (!num_acq_locks) MarkEndTCS(NULL);
    if (!num_acq_locks) MarkEndTCS(le);
    SignalHelper();
}

inline void FinishWrite(LogEntry * le, void * addr)
{
    assert(le);

#ifdef NVM_STATS
    int status;
    
    status = pthread_mutex_lock(&atlas_stats_wr_lock);
    assert(!status);

    ++total_logged_str_count;
    if (num_acq_locks > 0) ++cs_str_logged_count;

    status = pthread_mutex_unlock(&atlas_stats_wr_lock);
    assert(!status);
#endif    
        
    PublishLogEntry(le);
    tl_last_log_entry = le;
    SignalHelper();
}

bool canElideLogging()
{
    // if the thread-local table of undo locks is not yet created, it
    // means that critical sections haven't been executed by this thread
    // yet. So nothing needs to be undone.
    if (!tl_undo_locks) return true;

    typedef vector<MapOfLockInfo::iterator> ItrVec;
    ItrVec itr_vec;
    bool ret = true;
    MapOfLockInfo::iterator ci_end = tl_undo_locks->end();
    for (MapOfLockInfo::iterator ci = tl_undo_locks->begin();
         ci != ci_end; ++ ci)
    {
        void *lock_address = ci->first;
        uint64_t lock_count = ci->second;
        LockCount *lc = FindLockCount(lock_address);
        assert(lc);
        if (lc->Count_ <= lock_count)
        {
            ret = false;
            continue;
        }
        itr_vec.push_back(ci);
    }
    ItrVec::iterator itr_ci_end = itr_vec.end();
    for (ItrVec::iterator itr_ci = itr_vec.begin();
         itr_ci != itr_ci_end; ++ itr_ci)
        tl_undo_locks->erase(*itr_ci);
    return ret;
}

inline bool isAddrSizePairAlreadySeen(
    SetOfPairs **uniq_loc_p, void *addr, size_t sz)
{
    if (!*uniq_loc_p) *uniq_loc_p = new SetOfPairs;
    else if (FindSetOfPairs(**uniq_loc_p, addr, sz) != (*uniq_loc_p)->end())
        return true;
    InsertSetOfPairs(*uniq_loc_p, addr, sz);
    return false;
}
    
bool DoesNeedLogging(void *addr, size_t sz)
{
#if defined(_ALWAYS_LOG)
    return true;
#endif    
    
    // if inside a consistent section, logging can be elided if this
    // address/size pair has been seen before in this section
    if (num_acq_locks > 0)
    {
        // TODO need to evaluate for compiler-driven instrumentation. For
        // manually instrumented programs, this adds to the overhead
#ifdef _OPT_UNIQ_LOC
        if (isAddrSizePairAlreadySeen(&tl_uniq_loc, addr, sz))
        {
#ifdef NVM_STATS
            IncrementNotLoggedInCSStats();
#endif            
            return false;
        }
#endif        
        return true;
    }
    // If we are here, it means that this write is outside a critical section
#if defined(_SRRF) || defined(_NO_NEST)
    return false;
#endif    

    if (is_first_non_cs_stmt)
    {
        // Since we end a failure-atomic section at the end of a critical
        // section, this is also the first statement of the failure-
        // atomic section. Hence, this address/size pair couldn't have
        // been seen earlier.
        
        should_log_non_cs_stmt = !canElideLogging();
        is_first_non_cs_stmt = false;
        return should_log_non_cs_stmt;
    }
    else
    {
#ifdef _OPT_UNIQ_LOC
        if (should_log_non_cs_stmt)
            if (isAddrSizePairAlreadySeen(&tl_uniq_loc, addr, sz))
                return false;
#endif        
        return should_log_non_cs_stmt;
    }
    return true;
}

inline bool opt_log(void *addr, size_t sz)
{
    // TODO warn for the following scenario:
    // If the region table does not exist when we reach here, it means
    // that the compiler instrumented an access that is executed before
    // the region table is initialized. This can happen in legal situations
    // if the compiler cannot prove that this access is to a transient
    // memory location. But it can also happen if this is a synchronization
    // operation. In both these cases, it is ok not to log this operation.
    // But it could also be the result of incorrect programming where the
    // user specifies a non-existent persistent region or something like
    // that. So it is a good idea to warn.
    if (!region_table_addr) return true;
    
#if defined(_FLUSH_LOCAL_COMMIT) && !defined(DISABLE_FLUSHES) && \
    !defined(_DISABLE_DATA_FLUSH)
    if (!tl_flush_ptr) tl_flush_ptr = new SetOfInts;
    CollectCacheLines(tl_flush_ptr, addr, sz);
#endif
    if (!DoesNeedLogging(addr, sz))
    {
#ifdef NVM_STATS
        IncrementNotLoggedStats();
#endif
        return true;
    }
#ifdef NVM_STATS    
    if (!num_acq_locks) IncrementLogOptFailedStats();
#endif    
    return false;
}

// TODO: bit store support
void nvm_store(void *addr, size_t size)
{
    if (!NVM_IsInOpenPR(addr, size/8)) return;
    if (opt_log(addr, size/8)) return;
    LogEntry *le = CreateStrLogEntry(addr, size);
    FinishWrite(le, addr);
}

void nvm_memset(void *addr, size_t sz)
{
    if (!NVM_IsInOpenPR(addr, sz)) return;
    if (opt_log(addr, sz)) return;
    LogEntry *le = CreateLogEntry(addr, sz, LE_memset);
    FinishWrite(le, addr);
}

void nvm_memcpy(void *dst, size_t sz)
{
    if (!NVM_IsInOpenPR(dst, sz)) return;
    if (opt_log(dst, sz)) return;
    LogEntry *le = CreateLogEntry(dst, sz, LE_memcpy);
    FinishWrite(le, dst);
}

void nvm_memmove(void *dst, size_t sz)
{
    if (!NVM_IsInOpenPR(dst, sz)) return;
    if (opt_log(dst, sz)) return;
    LogEntry *le = CreateLogEntry(dst, sz, LE_memmove);
    FinishWrite(le, dst);
}

void nvm_barrier(void *p)
{
    if (!NVM_IsInOpenPR(p, 1)) return;
#if (!defined(DISABLE_FLUSHES) && !defined(_DISABLE_DATA_FLUSH))
    AO_nop_full();
    nvm_clflush((char*)p);
    AO_nop_full();
#endif    
}

void nvm_psync(void *start_addr, size_t sz)
{
    if (sz <= 0) return;

    char *last_addr = (char*)start_addr + sz - 1;

    char *cacheline_addr =
        (char*)(((uint64_t)start_addr) & cache_line_mask);
    char *last_cacheline_addr =
        (char*)(((uint64_t)last_addr) & cache_line_mask);

    AO_nop_full();
    do {
        NVM_CLFLUSH(cacheline_addr);
        cacheline_addr += cache_line_size;
    }while (cacheline_addr < last_cacheline_addr+1);
    AO_nop_full();
}

// TODO: The way the LLVM NVM instrumenter is working today, this introduces
// a bug since we are not checking whether start_addr is in the NVM space.
void nvm_psync_acq(void *start_addr, size_t sz)
{
    if (sz <= 0) return;

    char *last_addr = (char*)start_addr + sz - 1;

    char *cacheline_addr =
        (char*)(((uint64_t)start_addr) & cache_line_mask);
    char *last_cacheline_addr =
        (char*)(((uint64_t)last_addr) & cache_line_mask);

    AO_nop_full();
    do {
        NVM_CLFLUSH(cacheline_addr);
        cacheline_addr += cache_line_size;
    }while (cacheline_addr < last_cacheline_addr+1);
}

template <class T> T *SimpleHashTable<T>::Find(void *addr)
{
    ElemInfo *ei = GetElemInfoHeader(addr);
    while (ei)
    {
        if (ei->Addr_ == addr) return ei->Elem_;
        ei = ei->Next_;
    }
    return 0;
}

template <class T> void SimpleHashTable<T>::Insert(void *addr, const T & elem)
{
    ElemInfo *ei = GetElemInfoHeader(addr);
    while (ei)
    {
        if (ei->Addr_ == addr) ASRW(ei->Elem_, new T(elem)); 
        ei = ei->Next_;
    }
    ElemInfo *nei = new ElemInfo(addr, elem);
    ElemInfo *oei;
    // The following code assumes that no other thread is going to
    // insert an entry with the same address during the following loop,
    // otherwise we could end up with multiple entries with the same
    // key
    do
    {
        ei = GetElemInfoHeader(addr);
        nei->Next_ = ei;
        oei = (ElemInfo*)CAS(GetPointerToElemInfoHeader(addr), ei, nei);
    }while (oei != ei);
}

#if (defined(_FLUSH_LOCAL_COMMIT) || defined(_FLUSH_GLOBAL_COMMIT)) && \
    !(defined DISABLE_FLUSHES)

void CollectCacheLines(SetOfInts *cl_set, void *addr, size_t sz)
{
    char *last_addr = (char*)addr + sz - 1;
    char *line_addr = (char*)((uint64_t)addr & cache_line_mask);
    char *last_line_addr = (char*)((uint64_t)last_addr & cache_line_mask);
    do 
    {
        (*cl_set).insert((uint64_t)line_addr);
        line_addr += cache_line_size;
    }while (line_addr < last_line_addr+1);
}

void FlushCacheLines(const SetOfInts & cl_set)
{
    AO_nop_full();
    SetOfInts::const_iterator ci_end = cl_set.end();
    for (SetOfInts::const_iterator ci = cl_set.begin(); ci != ci_end; ++ ci)
    {
        assert(*ci);
        // We are assuming that a user persistent region is not closed
        // within a critical or atomic section.
#ifdef _FLUSH_GLOBAL_COMMIT
        // This is the only scenario today where the helper thread is
        // flushing data (i.e. essentially writing) data into a user
        // persistent region. But this region may have been closed by
        // the user by this point. So need to check for this situation.
        // This will at least prevent a fault but more needs to be done
        // to ensure consistency.
        if (!NVM_IsInOpenPR((void*)*ci, 1 /*dummy*/))
            continue;
#endif        
        NVM_CLFLUSH((char*)*ci);
    }
    AO_nop_full();
}

void FlushCacheLinesUnconstrained(const SetOfInts & cl_set)
{
    SetOfInts::const_iterator ci_end = cl_set.end();
    for (SetOfInts::const_iterator ci = cl_set.begin(); ci != ci_end; ++ ci)
    {
        assert(*ci);
        NVM_CLFLUSH((char*)*ci);
    }
}
#endif

#if defined(_FLUSH_LOCAL_COMMIT) && !defined(DISABLE_FLUSHES)
void FlushCacheLines()
{
    if (!tl_flush_ptr) return;
    if (tl_flush_ptr->empty()) return;
    FlushCacheLines(*tl_flush_ptr);
    tl_flush_ptr->clear();
    // TODO delete tl_flush_ptr during finalization. Currently difficult
    // since we don't have a good way of knowing when a thread is ending.
}
#endif

// TODO: the region may have been closed by this point. One solution is
// to check whether the address belongs to an open region before every
// clflush operation. This would slow down the helper thread but probably
// does not matter. Additionally, more logic needs to be employed in the
// helper thread in the global-flush mode so that if an address in a closed
// region is seen, the helper thread has to stop since no new consistent
// state can be found. This is tied with the observation that in this
// global mode, the consistent state cannot be moved forward during the
// recovery phase.
#if defined(_FLUSH_GLOBAL_COMMIT) && !defined(DISABLE_FLUSHES)
// This is not thread-safe. Currently, only the serial helper thread
// can call this interface.
extern SetOfInts *global_flush_ptr;
void FlushCacheLines(const LogEntryVec & logs)
{
#if 0 // This variation does not appear to buy any performance for
    // workloads tested with
    // This code was written with the older version which used a TcsTracker
    AO_nop_full();
    TcsTrackerVec::const_iterator ci_end = tcsvec.end();
    for (TcsTrackerVec::const_iterator ci = tcsvec.begin();
         ci != ci_end; ++ci)
    {
        SetOfInts *flush_set = TcsFlushHash.Find(*ci);
        if (flush_set) FlushCacheLinesUnconstrained(*flush_set);
    }
    AO_nop_full();
#endif

    assert(global_flush_ptr);
    assert(global_flush_ptr->empty());
    LogEntryVec::const_iterator ci_end = logs.end();
    for (LogEntryVec::const_iterator ci = logs.begin(); ci != ci_end; ++ci)
        if (isStr(*ci) || isMemop(*ci))
            CollectCacheLines(global_flush_ptr, (*ci)->Addr_, (*ci)->Size_);
    FlushCacheLines(*global_flush_ptr);
    global_flush_ptr->clear();
}

#endif
int NVM_GetCacheLineSize()
{
    int size;
    FILE * fp = fopen("/sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size", "r");
    if (fp) 
    {
        fscanf(fp, "%d", &size);
        fclose(fp);
    }
    else
    {
        size = DEFAULT_CACHE_LINE_SIZE;
        AtlasTrace(stderr, "WARNING: Config file not found: Using default cache line size: %d bytes\n", size);
    }
    return size;
}

void NVM_SetCacheParams() 
{
    cache_line_size = NVM_GetCacheLineSize();
    cache_line_mask = 0xffffffffffffffff - cache_line_size + 1;
}
    
void NVM_Initialize()
{
    fprintf(stderr, "-- Starting Atlas support --\n");
    NVM_SetCacheParams();

    // This should be the only call to SetupRegionTable regardless of
    // whether the table exists or not.
    extern const char *__progname;
    NVM_SetupRegionTable(__progname);
    RegionTableDump();
    
    const char *log_name = NVM_GetLogRegionName();

    if (NVM_doesLogExist(NVM_GetFullyQualifiedRegionName(log_name)))
    {
        fprintf(stderr,
                "The program crashed earlier, please run recovery ...\n");
        exit(0);
    }
    
    // Since this happens at init-time, we map the log file
    // from a well-known address, so that it does not conflict with other
    // data regions. This does not solve the general problem but this is
    // the least we could do.
    nvm_logs_id = NVM_CreateLogFile(log_name);
    log_structure_header_p = (LogStructure * volatile *)
        nvm_alloc(sizeof(LogStructure * volatile *), nvm_logs_id);
    assert(log_structure_header_p);
    *log_structure_header_p = 0;

    NVM_SetRegionRoot(nvm_logs_id, (void *)log_structure_header_p);


}

void NVM_Start_Consistency_Support(){

    pthread_cond_init(&helper_cond, NULL);
     // create the helper thread here
#if !defined(NDEBUG)            
    int status =
#endif        
        GC_pthread_create(&helper_thread, 0, (void *(*)(void *))helper, NULL);
    assert(!status);
   
}

void NVM_Finalize()
{
    pthread_mutex_lock(&helper_lock);
    ASRW(all_done, 1);
    pthread_mutex_unlock(&helper_lock);
    pthread_cond_signal(&helper_cond);

#if !defined(NDEBUG)            
    int status =
#endif        
        GC_pthread_join(helper_thread, 0);
    assert(!status);
    //KUMUD_TODO: GC_close
    NVM_DeleteRegion(NVM_GetLogRegionName());

    fprintf(stderr, "-- Finishing Atlas support --\n");
}

#ifdef NVM_STATS
void NVM_PrintStats()
{
    pthread_mutex_lock(&print_lock);
    fprintf(stderr, "Total # CS: %ld Nested # CS: %ld\n",
            total_cs_count, nested_cs_count);
    fprintf(stderr, "Total # logged str: %ld # CS logged str: %ld # Elided stores: %ld\n", 
            total_logged_str_count, cs_str_logged_count, total_strs_not_logged);
    fprintf(stderr, "Total # stores where logging elision failed: %ld\n",
            log_opt_failed_count);
    fprintf(stderr,
            "Total # stores not logged within consistent section: %ld\n",
            strs_in_cs_not_logged);
    fprintf(stderr, "Total # log entries: %ld\n",
            total_cs_count*2+total_logged_str_count);
    fprintf(stderr, "Total log mem usage: %ld\n", log_mem_use);
    pthread_mutex_unlock(&print_lock);
}

void NVM_PrintNumFlushes()
{
    fprintf(stderr, "Number of flushes from this thread is %ld\n",
            num_flushes);
}

#endif

void PrintLog()
{
    int status = pthread_mutex_lock(&print_lock);
    assert(!status);
    
    // Dump out the OwnerTable.
    fprintf(stderr,
            "Owner Table: <log-address, lock-address,cross-thread ptr>\n");
    fprintf(stderr,
            "---------------------------------------------------------\n");
    int i;
    for (i=0; i < TAB_SIZE; ++i)
    {
        OwnerInfo *header = (OwnerInfo*) OwnerTab[i];
        while (header != 0)
        {
            ImmutableInfo *ii = (ImmutableInfo*)ALAR(header->II_);
            assert(ii->LogAddr_);
            LogEntry *le = ii->LogAddr_;
            fprintf(stderr, "<%p,%p,%p>\n", le, le->Addr_,
                    (intptr_t*) le->ValueOrPtr_);
            header = header->Next_;
        }
    }
    fprintf(stderr, "\nLog structure headers:\n");
    fprintf(stderr, "----------------------\n");
    LogStructure * h = *log_structure_header_p;
    while (h)
    {
        fprintf(stderr, "<%p>: ", h->Le_);
        PrintLogEntry(h->Le_);
        h = h->Next_;
    }
    fprintf(stderr, "\n");

    status = pthread_mutex_unlock(&print_lock);
    assert(!status);
}

void PrintLogEntry(LogEntry *le)
{
    assert(le);
    while (le)
    {
        if (isAcquire(le))
            fprintf(stderr, "<((%p))%p:%p:%p:%lu:%d> ", 
                    le, le->Addr_, (void*)le->ValueOrPtr_, le->Next_,
                    le->Size_, le->Type_);
        else 
            fprintf(stderr, "<((%p))%p:%lu:%p:%lu:%d> ", 
                    le, le->Addr_, le->ValueOrPtr_, le->Next_,
                    le->Size_, le->Type_);
        le = le->Next_;
    }
    fprintf(stderr, "\n");
}
