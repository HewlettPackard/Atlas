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

#include "makalu_internal.h"

MAK_INNER MAK_persistent_memalign MAK_persistent_memalign_func = 0;

void* MAK_aflush_table[AFLUSH_TABLE_SZ] = {0};
void* MAK_fl_aflush_table[FL_AFLUSH_TABLE_SZ] = {0};

static __thread word clflush_count = 0;
static __thread word mfence_count = 0;
static word largest_n_logs = 0;
static word n_fas = 0;
static word total_logs = 0;
static word global_flush_count = 0;


MAK_API word MAK_CALL MAK_local_flush_count()
{
    return ((word) clflush_count);
}

MAK_API word MAK_CALL MAK_local_fence_count()
{
    return ((word) mfence_count);
}

MAK_API word MAK_CALL MAK_total_logs_count()
{
    return ((word) total_logs);
}

MAK_API word MAK_CALL MAK_total_fas_count()
{
    return ((word) n_fas);
}

MAK_API word MAK_CALL MAK_largest_fas_log_count()
{
    return ((word) largest_n_logs);
}

MAK_API word MAK_CALL MAK_total_flush_count()
{
    return ((word) global_flush_count);
}

#ifdef NVM_DEBUG
MAK_INNER void MAK_accumulate_flush_count()
{
     AO_fetch_and_add((AO_t*)(&global_flush_count), (AO_t) clflush_count);
}
#endif

#ifndef NO_CLFLUSH

MAK_INNER void flush_all_entry(void** tb, word tb_sz)
{
    MFENCE
    word i;
    void** cle;
    void* cl;
    //TODO: mfence
    for (i = 0; i < tb_sz; i++){
        cle = tb + i;
        cl = *cle;
        if (cl != NULL){
            CLFLUSH(cl);
            *cle = NULL;
        }
    }
    MFENCE
}

MAK_INNER void add_to_flush_table(void* start_addr, word size, void** table, word tb_sz)
{
   char* cl_start = (char*) (((word)(start_addr)) & (~(((word)CACHE_LINE_SZ) - 1))) ;
   char* end = (char*) start_addr + size;
   char* i;
   for (i = cl_start; i < end; i += CACHE_LINE_SZ){
       void** cle = table + (((word)(cl_start) >> LOG_CACHE_LINE_SZ)
                              & (((word)(tb_sz)) - 1));
       if ((word)(*cle) != (word) cl_start){
         if (*cle != NULL){
             CLFLUSH(*cle);
         }
         *cle = cl_start;
       }
   }
}

MAK_INNER void flush_range(char* start_addr, word size)
{
   char* cl_start = (char*) (((word)(start_addr)) & (~(((word)CACHE_LINE_SZ) - 1))) ;
   char* end = start_addr + size;
   char* i;
   MFENCE
   for (i = cl_start; i < end; i += CACHE_LINE_SZ){
       CLFLUSH(i);
   }
   MFENCE
}

#ifdef NVM_DEBUG

MAK_INNER void MAK_increment_clflush_count()
{
    //(void) AO_fetch_and_add((volatile void*)(&clflush_count), 1);
    clflush_count++;
}

MAK_INNER void MAK_increment_mfence_count()
{
    //oid) AO_fetch_and_add((volatile void*)(&mfence_count), 1);
    mfence_count++;
    //if (mfence_count == 2 && clflush_count == 0){
    //    MAK_printf("I need to know why mfence\n");
    //}
}


#endif // NVM_DEBUG

#endif // ! NO_CLFLUSH

#ifndef NO_NVM_LOGS

void* MAK_sflush_table[SFLUSH_TABLE_SZ] = {0};

#ifdef NVM_DEBUG
static word logging_in_session = 0;
static int log_anywhere = 0;
#endif

static log_e* curr_log_e = 0;


MAK_INNER void start_nvm_atomic() {
#ifdef NVM_DEBUG
   if (logging_in_session != 0){
       ABORT("Incomplete log found! Run recovery to resume\n");
   }
   logging_in_session = 1;
   n_fas++;
#endif
   curr_log_e = (log_e*) MAK_persistent_log_start;
}

MAK_INNER void end_nvm_atomic() {
   MAK_FLUSH_ALL_ENTRY(MAK_sflush_table, SFLUSH_TABLE_SZ);
   MAK_STORE_NVM_SYNC(MAK_persistent_log_version, MAK_persistent_log_version + 1);
#ifdef NVM_DEBUG
//   printf("persistent logging session: %d\n", MAK_persistent_log_version-1);
//   printf("Num of logs: %d\n", logging_in_session - 1);
//   word total_size = (logging_in_session-1) * LOG_E_SZ;
//   printf("Total size of logs: %d\n", total_size);
   if (logging_in_session - 1 > largest_n_logs){
       largest_n_logs = logging_in_session - 1;
   }
   total_logs += (logging_in_session - 1);
   logging_in_session = 0;
#endif
}

#ifdef NVM_DEBUG
  STATIC
#else
  MAK_INNER
#endif
 void create_int_log_entry(int* addr, int val) {
#ifdef NVM_DEBUG
   if ((char*) curr_log_e > MAK_persistent_log_start + MAX_LOG_SZ){
       ABORT("Run out of space to create a log entry\n");
   }
   logging_in_session++;
#endif
   curr_log_e->addr = (ptr_t) addr;
   curr_log_e->val.int_val = val;
   curr_log_e->type = INTEGER;
   curr_log_e->version = MAK_persistent_log_version;
   CLFLUSH_SYNC(curr_log_e);
   curr_log_e++;
}

#ifdef NVM_DEBUG
  STATIC
#else
  MAK_INNER
#endif
 void create_char_log_entry(unsigned char* addr, unsigned char val) {
#ifdef NVM_DEBUG
   if ((char*) curr_log_e > MAK_persistent_log_start + MAX_LOG_SZ){
       ABORT("Run out of space to create a log entry\n");
   }
   logging_in_session++;
#endif
   curr_log_e->addr = (ptr_t) addr;
   curr_log_e->val.char_val = val;
   curr_log_e->type = CHAR;
   curr_log_e->version = MAK_persistent_log_version;
   CLFLUSH_SYNC(curr_log_e);
   curr_log_e++;
}

#ifdef NVM_DEBUG
  STATIC
#else
  MAK_INNER
#endif
 void create_addr_log_entry(void** addr, void* val) {
#ifdef NVM_DEBUG
   if ((char*) curr_log_e > MAK_persistent_log_start + MAX_LOG_SZ){
       ABORT("Run out of space to create a log entry\n");
   }
   logging_in_session++;
#endif
   curr_log_e->addr = (ptr_t) addr;
   curr_log_e->val.addr_val = (ptr_t) val;
   curr_log_e->type = ADDR;
   curr_log_e->version = MAK_persistent_log_version;
   CLFLUSH_SYNC(curr_log_e);
   curr_log_e++;
}

#ifdef NVM_DEBUG
  STATIC
#else
  MAK_INNER
#endif
 void create_word_log_entry(word* addr, word val) {
#ifdef NVM_DEBUG
   if ((char*) curr_log_e > MAK_persistent_log_start + MAX_LOG_SZ){
       ABORT("Run out of space to create a log entry\n");
   }
   logging_in_session++;
#endif
   curr_log_e->addr = (ptr_t) addr;
   curr_log_e->val.word_val = val;
   curr_log_e->type = WORD;
   curr_log_e->version = MAK_persistent_log_version;
   CLFLUSH_SYNC(curr_log_e);
   curr_log_e++;
}

#ifdef NVM_DEBUG


MAK_INNER void store_nvm_int(int* addr, int val){
    if (!logging_in_session && !log_anywhere){
       ABORT("Aborting: Cannot log outside an active log\n");
    }
    if (logging_in_session)
    create_int_log_entry(addr, *(addr));
    *(addr) = (val);
    CLFLUSH_FAS(addr, sizeof(int));
}

MAK_INNER void store_nvm_char(unsigned char* addr, unsigned char val){
    if (!logging_in_session && !log_anywhere){
       ABORT("Aborting: Cannot log outside an active log\n");
    }
    if (logging_in_session)
    create_char_log_entry(addr, *(addr));
    *(addr) = (val);
    CLFLUSH_FAS(addr, sizeof(unsigned char));
}

MAK_INNER void store_nvm_addr(void** addr, void* val){
    if (!logging_in_session && !log_anywhere){
       ABORT("Aborting: Cannot log outside an active log\n");
       exit(EXIT_FAILURE);
    }
    if (logging_in_session)
    create_addr_log_entry(addr, *(addr));
    *(addr) = (val);
    CLFLUSH_FAS(addr, sizeof(void*));
}

MAK_INNER void store_nvm_word(word* addr, word val){
    if (!logging_in_session && !log_anywhere){
       ABORT("Aborting: Cannot log outside an active log\n");
       exit(EXIT_FAILURE);
    }
    if (logging_in_session)
    create_word_log_entry(addr, *(addr));
    *(addr) = (val);
    CLFLUSH_FAS(addr, sizeof(word));
}

MAK_INNER void log_nvm_int(int* addr, int val){
    if (!logging_in_session && !log_anywhere){
       ABORT("Aborting: Cannot log outside an active log\n");
    }
    if (logging_in_session)
    create_int_log_entry(addr, val);
}

MAK_INNER void log_nvm_char(unsigned char* addr, unsigned char val){
    if (!logging_in_session && !log_anywhere){
       ABORT("Aborting: Cannot log outside an active log\n");
    }
    if (logging_in_session)
    create_char_log_entry(addr, val);
}

MAK_INNER void log_nvm_addr(void** addr, void* val){
    if (!logging_in_session && !log_anywhere){
       ABORT("Aborting: Cannot log outside an active log\n");
       exit(EXIT_FAILURE);
    }
    if (logging_in_session)
    create_addr_log_entry(addr, val);
}

MAK_INNER void log_nvm_word(word* addr, word val){
    if (!logging_in_session && !log_anywhere){
       ABORT("Aborting: Cannot log outside an active log\n");
       exit(EXIT_FAILURE);
    }
    if (logging_in_session)
    create_word_log_entry(addr, val);
}
#endif // NVM_DEBUG

#endif  //NO_NVM_LOGS

MAK_INNER void MAK_init_persistent_logs(){
    int res = GET_MEM_PERSISTENT(&(MAK_persistent_log_start), MAX_LOG_SZ);
    if (res != 0)
        ABORT("Could not allocate space for persistent log!\n");
    // reset_persistent_log();
    BZERO(MAK_persistent_log_start, MAX_LOG_SZ);
    MAK_NVM_ASYNC_RANGE(MAK_persistent_log_start, MAX_LOG_SZ);
    //visibility of below will be taken care of by MAK_arrays flushing
    MAK_persistent_log_version = 1;
}


#ifndef NO_NVM_LOGS
MAK_INNER void MAK_recover_metadata(){
   log_e* e = (log_e*) MAK_persistent_log_start;
   //seek to the last written valid entry.
   while (e->version == MAK_persistent_log_version) e++;
   e -= 1;
   while ((char*)e >= MAK_persistent_log_start) {
      switch (e->type){
          case INTEGER:
             *((int*)(e->addr)) = e->val.int_val;
             CLFLUSH_FAS((int*)(e -> addr), sizeof(int));
             break;
          case CHAR:
             *((unsigned char*)(e->addr)) = e->val.char_val;
             CLFLUSH_FAS((unsigned char*)(e -> addr), sizeof(unsigned char));
             break;
          case ADDR:
             *((void**)(e->addr)) = (void*) (e->val.addr_val);
             CLFLUSH_FAS((void**)(e -> addr), sizeof(void*));
             break;
          case WORD:
             *((word*)(e->addr)) = e->val.word_val;
             CLFLUSH_FAS((word*)(e -> addr), sizeof(word));
             break;
          default:
             ABORT("Aborting: Type of value not supported in log\n");
      }
      e -= 1;
   }
   MAK_FLUSH_ALL_ENTRY(MAK_sflush_table, SFLUSH_TABLE_SZ);
   MAK_STORE_NVM_SYNC(MAK_persistent_log_version, MAK_persistent_log_version + 1);
   //TODO: reset the log version to avoid overflow
   //TODO: probably should be doing in every restart
   //reset_persistent_log();
}
#endif

MAK_INNER void MAK_sync_all_persistent(){
    //flush the table
    MAK_FLUSH_ALL_ENTRY(MAK_aflush_table, AFLUSH_TABLE_SZ);
}

MAK_INNER void MAK_sync_alloc_metadata(){

   if (MAK_persistent_state == PERSISTENT_STATE_NONE)
       return;

   MAK_NVM_ASYNC_RANGE(&MAK_hdr_idx_free_ptr, sizeof(ptr_t));
   MAK_NVM_ASYNC_RANGE(&MAK_hdr_free_list, sizeof(hdr*));
   //flush the table
   MAK_FLUSH_ALL_ENTRY(MAK_aflush_table, AFLUSH_TABLE_SZ);
   MAK_STORE_NVM_SYNC(MAK_persistent_state, PERSISTENT_STATE_NONE);
}

MAK_INNER void MAK_sync_gc_metadata(){

   if (!MAK_mandatory_gc)
       return;
   //flush the table
   MAK_FLUSH_ALL_ENTRY(MAK_aflush_table, AFLUSH_TABLE_SZ);
   MAK_FLUSH_ALL_ENTRY(MAK_fl_aflush_table, FL_AFLUSH_TABLE_SZ);
   MAK_STORE_NVM_SYNC(MAK_mandatory_gc, FALSE);
}

