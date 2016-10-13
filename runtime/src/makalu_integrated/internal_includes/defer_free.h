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
 

#ifndef DEFER_FREE_H
#define DEFER_FREE_H

#include <stdint.h>
#include <pthread.h>

typedef uint64_t word;

#define PAGE_BYTES 4096
#define CONTAINER_BYTES (PAGE_BYTES / 4)
#define PTR_BYTES (sizeof(uint64_t))
#define LOG_PTR_BYTES 3
#define ELEMS_PER_CONTAINER ((CONTAINER_BYTES / PTR_BYTES) - 2)
#define DEFERRED_FREE_TABLE_SZ 64
#define HASH_IDX_DEFERRED_FREE_TAB(x) ((((word) x) >> LOG_PTR_BYTES) & ((word)DEFERRED_FREE_TABLE_SZ - 1))
#define THR_CONTEXT_TABLE_SZ 256
#define HASH_IDX_THR_CONTEXT_TAB(x) ((((word)x) >> LOG_PTR_BYTES) & ((word) THR_CONTEXT_TABLE_SZ - 1))

#define INSERT_ADDR_INTO_BASKET(basketPtr, addr) \
{ \
    if (basketPtr == NULL || basketPtr -> count == ELEMS_PER_CONTAINER) { \
        AddrBasketPtr bptr = allocAddrBasket(); \
        bptr -> nextBasket = basketPtr; \
        basketPtr = bptr; \
    } \
    word* count = &(basketPtr -> count); \
    (basketPtr -> addresses)[*count] = addr; \
    *count = *count + 1; \
}

#define LOCK(x) \
    { \
        int loc_ret_val = pthread_mutex_lock((x)); \
        if (loc_ret_val != 0) \
            std::cout << "Something went wrong while trying to acquire a lock" << std:: endl; \
    }

#define UNLOCK(x) \
    { \
        int loc_ret_val = pthread_mutex_unlock((x)); \
        if (loc_ret_val != 0) \
            std::cout << "Something went wrong while trying to relese a lock" << std:: endl; \
    }

typedef struct AddressBasket {
    void* addresses[ELEMS_PER_CONTAINER];
    word count;
    AddressBasket* nextBasket;
}* AddrBasketPtr;

//Node corresponding to ocs
typedef struct DeferredFreeNode {
    //ocs identifier
    void* key;        

    //linked list of containers containing dealloced addresses
    AddrBasketPtr freeAddresses;  

    DeferredFreeNode* next;  
}* DefFreeNodePtr;


typedef struct ThreadContext {
    void* key;
    DefFreeNodePtr deferredFreeTable[DEFERRED_FREE_TABLE_SZ];
    //per bucket lock for the above table
    pthread_mutex_t deferredFreeTabLocks[DEFERRED_FREE_TABLE_SZ];
    
    //lock to hold when manpulating following
    //freelists
    pthread_mutex_t deferredFreeTabAllocLock;
    //freelist of AddressBasket
    AddrBasketPtr addrBasketFl;
   
    //freelist of Node corresponding to ocs
    DefFreeNodePtr defFreeNodeFl;

    ThreadContext* next;
}* ThreadContextPtr;

extern "C" {

    typedef int (*GC_persistent_memalign)(void** memptr,
              size_t alignment, size_t size);
    void* GC_init_persistent(GC_persistent_memalign memalign_func);
    #define GC_INIT_PERSISTENT(memalign_func) GC_init_persistent( \
                       memalign_func)
    void GC_restart_offline(char* start_addr,
              GC_persistent_memalign memalign_func);
    #define GC_RESTART_OFFLINE(start_addr, memalign_func) \
      GC_restart_offline(start_addr, memalign_func)

    void GC_restart_online(char* start_addr,
                GC_persistent_memalign memalign_func);
    #define GC_RESTART_ONLINE(start_addr, memalign_func) \
       GC_restart_online(start_addr, memalign_func)

    void GC_set_collect_offline_only(unsigned int flag);

    int GC_try_to_collect_offline();
       
    void GC_set_defer_free_fn(void (*defer_free_callback_fn)(pthread_t, void*));   
    void GC_free_imm(void* addr);
    void GC_declare_never_allocate(int val);

    int GC_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start_routine) (void *), void *arg);
    int GC_pthread_join(pthread_t thread, void **value_ptr);

    void GC_close(void);
    #define GC_CLOSE() GC_close()

}

/* Called by mem allocator everytime a free is called */
void DeferFreeCallback(pthread_t id, void* addr);

/*At the end of the ocs, create a node with a given key */
/* in the deferredFreeTable with all the frees within the ocs */
void CreateDeferredFreeNode(void* key);

/* Called by helper thread with ocs identifier as key */
void ProcessDeferredFrees(void* context, void* key);

/* Allocates the ThreadContext, caches the pointers to its members thread locally */
ThreadContextPtr InitThreadContext();

/* Puts the ThreadContext into the ThreadContext Table */
/* associating it with the key that */
/* identifies the thread (the LogHead structure??) */
void SetThreadContext(void* key, ThreadContextPtr context);

/* Removes the thread context from the table and returns it. */
/* Expects the caller to deallocate the ThreadContext */
/* no longer necessary */
ThreadContextPtr RemoveThreadContext(void* key);

/* Returns thread context corresponding to the key */ 
ThreadContextPtr LookUpThreadContext(void* key);

/* Associates the ThreadContext associated with oldKey to newKey */
/* Called when identifier for the thread (e.g.LogHeads) changes */
/* for the thread */
void UpdateThreadContext(void* oldKey, void* newKey);

#endif
