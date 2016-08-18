#ifndef _MAKALU_PERSISTENT_H
#define _MAKALU_PERSISTENT_H

typedef struct log_e {
    ptr_t addr;
    char type;
    union {
       unsigned char     char_val;
       ptr_t addr_val;
       word     word_val;
       int      int_val;
    } val;
    unsigned long version;
    /*so that each entry is a single cache line */
    char dummy[32];  
} log_e;

#define LOG_E_SZ sizeof(log_e)

#define MAX_NUM_LOG_E (unsigned int) MAX_LOG_SZ / (unsigned int) LOG_E_SZ

MAK_EXTERN MAK_persistent_memalign MAK_persistent_memalign_func;

MAK_EXTERN void* MAK_aflush_table[AFLUSH_TABLE_SZ];
MAK_EXTERN void* MAK_fl_aflush_table[FL_AFLUSH_TABLE_SZ];

#ifndef NO_NVM_LOGS
  /*used to accumulate changes to the cache line within a FAS before flushing
  note that the logs still has to be flushed before the data is modified. */
  MAK_EXTERN void* MAK_sflush_table[SFLUSH_TABLE_SZ];
#endif

#ifdef NO_CLFLUSH

  #define MFENCE

//cache line flushes
  #define CLFLUSH(addr) 

  #define CLFLUSH_SYNC(addr)  

#ifndef NO_NVM_LOGS
  #define CLFLUSH_FAS(addr, size) 
#endif

//  #define CLFLUSH_ASYNC(addr, size)

  //store with immediate flushing
  #define MAK_NVM_SYNC_RANGE(start_addr, size) 

  #define MAK_NVM_ASYNC_RANGE(addr, size)  

  #define MAK_NVM_ASYNC_RANGE_VIA(start_addr, size, \
        aflush_tb, aflush_tb_sz) 

  #define MAK_FLUSH_ALL_ENTRY(table, size)

  #ifndef NO_NVM_LOGS
      #define CLFLUSH_FAS(addr, size) 
  #endif


#else // ! NO_CLFLUSH

#ifdef NVM_DEBUG
    //thread safe, increments the clflush counter
    #define MAK_INCREMENT_CLFLUSH_COUNT() MAK_increment_clflush_count()
    MAK_INNER void MAK_increment_clflush_count();
    #define MAK_INCREMENT_MFENCE_COUNT() MAK_increment_mfence_count()
    MAK_INNER void MAK_increment_mfence_count();

#else
    #define MAK_INCREMENT_CLFLUSH_COUNT()
    #define MAK_INCREMENT_MFENCE_COUNT()
#endif

   //copy the above and redine it here
   #define MFENCE \
   { \
       MAK_INCREMENT_MFENCE_COUNT(); \
       __asm__ __volatile__ ("mfence" ::: "memory");  \
   }

   #define CLFLUSH(addr) \
   { \
       __asm__ __volatile__ (   \
       "clflush %0 \n" : "+m" (*(char*)(addr))  \
       ); \
       MAK_INCREMENT_CLFLUSH_COUNT(); \
   }

   #define CLFLUSH_SYNC(addr) \
   { \
      MFENCE \
      CLFLUSH(addr); \
      MFENCE \
   }
#ifndef NO_NVM_LOGS
   #define CLFLUSH_FAS(addr, size) \
   { \
      add_to_flush_table(addr, size, MAK_sflush_table, SFLUSH_TABLE_SZ); \
   }
#endif

   #define MAK_FLUSH_RANGE(start_addr, size) flush_range((char*)(start_addr), size)
   MAK_INNER void flush_range(char* start_addr, word size);

   //store with immediate flushing
   #define MAK_NVM_SYNC_RANGE(start_addr, size) \
   { \
      MAK_FLUSH_RANGE(start_addr, size); \
   }

   #define MAK_NVM_ASYNC_RANGE(addr, size) \
   { \
      add_to_flush_table(addr, size, MAK_aflush_table, AFLUSH_TABLE_SZ); \
   }

   #define MAK_NVM_ASYNC_RANGE_VIA(start_addr, size, \
        aflush_tb, aflush_tb_sz) \
   { \
      add_to_flush_table(start_addr, size, aflush_tb, aflush_tb_sz); \
   }

   #define MAK_FLUSH_ALL_ENTRY(table, size) flush_all_entry(table, size)
   MAK_INNER void flush_all_entry(void** table, word sz);

   MAK_INNER void add_to_flush_table(void* start_addr, word size, void** table, word tb_sz);

#endif  // ! NO_CLFLUSH

#ifdef NO_NVM_LOGS
  #define MAK_START_NVM_ATOMIC  

  #define MAK_END_NVM_ATOMIC 

  // store without logging within FAS
  #define MAK_NO_LOG_STORE_NVM(var, val) \
  { \
    var = val;  \
  }

  #define MAK_NO_LOG_NVM_RANGE(addr, size) 

  #define JUST_STORE(addr, val) *(addr) = (val)

  #define MAK_STORE_NVM_INT(addr, val) JUST_STORE(addr, val)

  #define MAK_STORE_NVM_CHAR(addr, val) JUST_STORE(addr, val);

  #define MAK_STORE_NVM_ADDR(addr, val) JUST_STORE((void**) (addr), (void*) (val))

  #define MAK_STORE_NVM_WORD(addr, val) JUST_STORE(addr, val)

  //log without storing is no-op
  #define MAK_LOG_NVM_INT(addr, val)
  #define MAK_LOG_NVM_CHAR(addr, val)
  #define MAK_LOG_NVM_ADDR(addr, val)
  #define MAK_LOG_NVM_WORD(addr, val)

#else  // ! NO_NVM_LOGS

  #define MAK_START_NVM_ATOMIC start_nvm_atomic()
  MAK_INNER void start_nvm_atomic();

  #define MAK_END_NVM_ATOMIC end_nvm_atomic()
  MAK_INNER void end_nvm_atomic();

  // store without logging within FAS
  #define MAK_NO_LOG_STORE_NVM(var, val) \
  { \
    var = val; \
    CLFLUSH_FAS(&(var), sizeof(word)); \
  }

  #define MAK_NO_LOG_NVM_RANGE(addr, size) CLFLUSH_FAS(addr, size)

#ifdef NVM_DEBUG  

//store with logging

  #define MAK_STORE_NVM_INT(addr, val) store_nvm_int(addr, val)
  MAK_INNER void store_nvm_int(int* addr, int val);

  #define MAK_STORE_NVM_CHAR(addr, val) store_nvm_char(addr, val)
  MAK_INNER void store_nvm_char(unsigned char* addr, unsigned char val);

  #define MAK_STORE_NVM_ADDR(addr, val) store_nvm_addr((void**)addr, (void*) val)
  MAK_INNER void store_nvm_addr(void** addr, void* val);

  #define MAK_STORE_NVM_WORD(addr, val) store_nvm_word(addr, val)
  MAK_INNER void store_nvm_word(word* addr, word val);

  // log without storing

  #define MAK_LOG_NVM_INT(addr, val) log_nvm_int(addr, val)
  MAK_INNER void log_nvm_int(int* addr, int val);

  #define MAK_LOG_NVM_CHAR(addr, val) log_nvm_char(addr, val)
  MAK_INNER void log_nvm_char(unsigned char* addr, unsigned char val);

  #define MAK_LOG_NVM_ADDR(addr, val) log_nvm_addr((void**)addr, (void*) val)
  MAK_INNER void log_nvm_addr(void** addr, void* val);

  #define MAK_LOG_NVM_WORD(addr, val) log_nvm_word(addr, val)
  MAK_INNER void log_nvm_word(word* addr, word val);

#else // !NVM_DEBUG
  MAK_INNER void create_int_log_entry(int* addr, int val);
  MAK_INNER void create_char_log_entry(unsigned char* addr, unsigned char val);
  MAK_INNER void create_addr_log_entry(void** addr, void* val);
  MAK_INNER void create_word_log_entry(word* addr, word val);

   #define MAK_STORE_NVM_INT(addr, val) \
   { \
       create_int_log_entry(addr, *(addr)); \
       *(addr) = (val); \
       CLFLUSH_FAS(addr, sizeof(int)); \
   }

   #define MAK_STORE_NVM_CHAR(addr, val) \
   { \
       create_char_log_entry(addr, *(addr)); \
       *(addr) = (val); \
       CLFLUSH_FAS(addr, sizeof(char)); \
   }

   #define MAK_STORE_NVM_ADDR(addr, val) \
   { \
       create_addr_log_entry((void**) (addr), (*((void**)(addr)))); \
       *((void**) (addr)) = ((void*) (val)); \
       CLFLUSH_FAS(addr, sizeof(void*)); \
   }

   #define MAK_STORE_NVM_WORD(addr, val) \
   { \
       create_word_log_entry(addr, *(addr)); \
       *(addr) = (val); \
       CLFLUSH_FAS(addr, sizeof(word)); \
   }

   #define MAK_LOG_NVM_INT(addr, val)  create_int_log_entry(addr, val)
   #define MAK_LOG_NVM_CHAR(addr, val) create_char_log_entry(addr, val)
   #define MAK_LOG_NVM_ADDR(addr, val) create_addr_log_entry((void**) (addr), (void*) (val))
   #define MAK_LOG_NVM_WORD(addr, val) create_word_log_entry(addr, val)
#endif //NVM_DEBUG

#endif //NO_NVM_LOGS

//store with immediate flushing
#define MAK_STORE_NVM_SYNC(var, val) \
    (var) = (val); \
    CLFLUSH_SYNC(&(var));

#define MAK_STORE_NVM_PTR_SYNC(var, ptr) \
    *(var) = (ptr); \
    CLFLUSH_SYNC(var);

//store with async flushing

#define MAK_STORE_NVM_ASYNC(var, val) \
    var = (val); \
    MAK_NVM_ASYNC_RANGE(&(var), sizeof(word));

#define MAK_STORE_NVM_PTR_ASYNC(var, ptr) \
    *(var) = (ptr); \
    MAK_NVM_ASYNC_RANGE(var, sizeof(void*));


#ifdef NVM_DEBUG
    #define MAK_ACCUMULATE_FLUSH_COUNT() MAK_accumulate_flush_count()
    MAK_INNER void MAK_accumulate_flush_count();
#else
    #define MAK_ACCUMULATE_FLUSH_COUNT()
#endif

//intialization and recovery

MAK_INNER void MAK_init_persistent_log();

#ifdef NO_NVM_LOGS
  #define MAK_RECOVER_METADATA() 
#else
  #define MAK_RECOVER_METADATA() MAK_recover_metadata()
  MAK_INNER void MAK_recover_metadata();
#endif

MAK_INNER void MAK_init_persistent_logs();
MAK_INNER void MAK_sync_all_persistent();
MAK_INNER void MAK_sync_alloc_metadata();
MAK_INNER void MAK_sync_gc_metadata();

# define GET_MEM_PERSISTENT(addr, bytes) \
       (*MAK_persistent_memalign_func) ((void**) addr, MAK_page_size, bytes)

# define GET_MEM_PERSISTENT_PTRALIGN(addr, bytes) \
       (*MAK_persistent_memalign_func)((void**) addr, sizeof(ptr_t), bytes)

#endif //_MAKALU_PERSISTENT_H
