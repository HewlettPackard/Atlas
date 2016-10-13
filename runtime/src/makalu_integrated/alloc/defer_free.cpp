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
 

#include <defer_free.h>
#include <log_structure.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>


__thread DefFreeNodePtr* DeferredFreeTable = NULL;
__thread pthread_mutex_t* DeferredFreeTabLocks = NULL;
__thread pthread_mutex_t* DeferredFreeTabAllocLock = NULL;
__thread AddrBasketPtr* AddrBasketFlPtr = NULL;
__thread DefFreeNodePtr* DefFreeNodeFlPtr = NULL;
__thread AddrBasketPtr CurrAddrBasket = NULL;

//DefFreeNodePtr DeferredFreeTable[DEFERRED_FREE_TABLE_SZ] = {0};
//pthread_mutex_t DeferredFreeTabLocks[DEFERRED_FREE_TABLE_SZ];
//pthread_mutex_t DeferredFreeTabAllocLock = PTHREAD_MUTEX_INITIALIZER;

static inline DefFreeNodePtr allocDefFreeNode(){
    DefFreeNodePtr ret;
    LOCK(DeferredFreeTabAllocLock);
    if ((ret = *DefFreeNodeFlPtr) != NULL){
        *DefFreeNodeFlPtr = ret -> next;
        UNLOCK(DeferredFreeTabAllocLock);
    }
    else {
        UNLOCK(DeferredFreeTabAllocLock);
        ret = (DefFreeNodePtr) malloc(sizeof(DeferredFreeNode));
    }
    memset(ret, 0, sizeof(DeferredFreeNode));
    //std::cout << "Allocated Def Node: " << ret << std::endl;
    return ret;
}

static inline void deleteDefFreeNode(ThreadContextPtr context, DefFreeNodePtr nodePtr){
    pthread_mutex_t* lck = &(context -> deferredFreeTabAllocLock);
    LOCK(lck);
    nodePtr -> next = context -> defFreeNodeFl;
    context -> defFreeNodeFl = nodePtr;
    UNLOCK(lck);
}

static inline void insertDefFreeNode(DefFreeNodePtr nodePtr){
    word idx = HASH_IDX_DEFERRED_FREE_TAB(nodePtr -> key);
    LOCK(&(DeferredFreeTabLocks[idx]));
    DefFreeNodePtr* currEntryPtr = &DeferredFreeTable[idx];
    nodePtr -> next = *currEntryPtr;
    *currEntryPtr = nodePtr;
    UNLOCK(&(DeferredFreeTabLocks[idx]));
}

static inline DefFreeNodePtr removeDefFreeNode(ThreadContextPtr context, void* key){
    word idx = HASH_IDX_DEFERRED_FREE_TAB(key);
    pthread_mutex_t* lck = &(context -> deferredFreeTabLocks[idx]);
    DefFreeNodePtr prev = NULL;
    LOCK(lck);
    DefFreeNodePtr* headPtr= &(context -> deferredFreeTable[idx]);
    DefFreeNodePtr curr = *headPtr;
    while (curr != NULL){
        if (curr -> key == key){
            if (curr == *headPtr){
                *headPtr = curr -> next;
            }
            else {
                prev -> next = curr -> next;
            }
            UNLOCK(lck);
            return curr;
        }
        prev = curr;
        curr = curr -> next;
    }
    UNLOCK(lck);
    return NULL;
}

static inline AddrBasketPtr allocAddrBasket(){
    AddrBasketPtr ret;
    LOCK(DeferredFreeTabAllocLock);
    if ((ret = *AddrBasketFlPtr) != NULL){
        *AddrBasketFlPtr = ret -> nextBasket;
        UNLOCK(DeferredFreeTabAllocLock);
    } else {
        UNLOCK(DeferredFreeTabAllocLock);
        ret = (AddrBasketPtr) malloc(sizeof(AddressBasket));
    }

    memset(ret, 0, sizeof(AddressBasket));
    //std::cout << "Allocated Address Basket: " << ret << std::endl;
    return ret;
}

static inline void deleteAddrBasket(ThreadContextPtr context, AddrBasketPtr start, AddrBasketPtr end){
    pthread_mutex_t* lck = &(context -> deferredFreeTabAllocLock); 
    LOCK(lck);
    end -> nextBasket = (context -> addrBasketFl);
    context -> addrBasketFl = start;
    UNLOCK(lck);
}

void CreateDeferredFreeNode(void* key){
    if (CurrAddrBasket == NULL) return;
    DefFreeNodePtr node = allocDefFreeNode();
    node -> key = key;
    node -> freeAddresses = CurrAddrBasket;
    CurrAddrBasket = NULL;
    insertDefFreeNode(node);
}

void ProcessDeferredFrees(void* context, void* key){
    
    DefFreeNodePtr defNodePtr = removeDefFreeNode((ThreadContextPtr) context, key);
    if (defNodePtr == NULL) return;
    AddrBasketPtr start = defNodePtr -> freeAddresses;
    AddrBasketPtr curr = start;
    AddrBasketPtr prev = NULL;
    while (curr != NULL){
        for (word i = 0; i < curr -> count; i++){
            GC_free_imm((curr -> addresses)[i]);
        }
        //std::cout << "---------\n";
        prev = curr;
        curr = curr -> nextBasket;
    }
    deleteAddrBasket((ThreadContextPtr) context, start, prev);
    deleteDefFreeNode((ThreadContextPtr) context,  defNodePtr);
}

ThreadContextPtr InitThreadContext(){
    ThreadContextPtr context = (ThreadContextPtr) malloc(sizeof(ThreadContext));
    //printf("Setting thread context for %ld\n",(long) pthread_self());
    memset(context, 0, sizeof(ThreadContext));

    //cache the fields thread locally
    DeferredFreeTable =        &(context -> deferredFreeTable[0]);
    DeferredFreeTabLocks =     &(context -> deferredFreeTabLocks[0]);
    DeferredFreeTabAllocLock = &(context -> deferredFreeTabAllocLock);
    AddrBasketFlPtr =          &(context -> addrBasketFl);
    DefFreeNodeFlPtr =         &(context -> defFreeNodeFl);    

    assert(context);
    return context;
}


static ThreadContextPtr ThreadContextTable[THR_CONTEXT_TABLE_SZ] = {0};
static pthread_mutex_t ThreadContextTabLocks[THR_CONTEXT_TABLE_SZ] = {0};


void SetThreadContext(void* key, ThreadContextPtr context){
    assert(key && context);
    context -> key = key;
    word idx = HASH_IDX_THR_CONTEXT_TAB(key);
    LOCK(&(ThreadContextTabLocks[idx]));
    ThreadContextPtr* head = &(ThreadContextTable[idx]);
    context -> next = *head;
    *head = context;
    UNLOCK(&(ThreadContextTabLocks[idx]));
}

ThreadContextPtr RemoveThreadContext(void* key){
    ThreadContextPtr prev = NULL;
    word idx = HASH_IDX_THR_CONTEXT_TAB(key);
    LOCK(&(ThreadContextTabLocks[idx]));
    ThreadContextPtr* headPtr = &(ThreadContextTable[idx]);
    ThreadContextPtr curr = *headPtr;
    while (curr != NULL){
        if (curr -> key == key){
            if (curr == *headPtr){
                *headPtr = curr -> next;
            }
            else {
                prev -> next = curr -> next;
            }
            UNLOCK(&(ThreadContextTabLocks[idx]));
            return curr;
        }
        prev = curr;
        curr = curr -> next;
    }
    UNLOCK(&(ThreadContextTabLocks[idx]));
    assert(0);  //we should have found it
    return NULL;
}

ThreadContextPtr LookUpThreadContext(void* key){
    word idx = HASH_IDX_THR_CONTEXT_TAB(key);
    LOCK(&(ThreadContextTabLocks[idx]));
    ThreadContextPtr curr = ThreadContextTable[idx];
    while (curr != NULL){
        if (curr -> key == key){
            UNLOCK(&(ThreadContextTabLocks[idx]));
            return curr;
        }
        curr = curr -> next;
    }
    UNLOCK(&(ThreadContextTabLocks[idx]));
    assert(0); //we should only be looking for the ones we put in
    return NULL;
}

void UpdateThreadContext(void* oldKey, void* newKey){
    assert(oldKey && newKey);
    ThreadContextPtr context = RemoveThreadContext(oldKey);
    assert(context);
    SetThreadContext (newKey, context);
}

void DeferFreeCallback(pthread_t id, void* addr)
{
    if(DoesNeedLogging(addr, sizeof(word))){
        INSERT_ADDR_INTO_BASKET(CurrAddrBasket, addr);
    } 
    else {
        GC_free_imm(addr);
    }
}

#if 0
int main() {
    ThreadContextPtr context = InitThreadContext();
    for (word i =0; i < ELEMS_PER_CONTAINER * 2; i++) {
        INSERT_ADDR_INTO_BASKET(CurrAddrBasket, (void *) (i + 1));
    }
    CreateDeferredFreeNode((void*) 0x20a3b7);

    for (word i =0; i < ELEMS_PER_CONTAINER * 2; i++) {
        INSERT_ADDR_INTO_BASKET(CurrAddrBasket, (void *) (i + 1));
    }
    CreateDeferredFreeNode((void*) 0xffb7);

    ProcessDeferredFrees(context, (void*) 0x20a3b7);
    ProcessDeferredFrees(context, (void*) 0xffb7);

    allocDefFreeNode();
    allocDefFreeNode();

    allocAddrBasket();
    allocAddrBasket();
    allocAddrBasket();
    allocAddrBasket();


    std::cout << HASH_IDX_DEFERRED_FREE_TAB(0) << std::endl;
    std::cout << HASH_IDX_DEFERRED_FREE_TAB(0x20a3b7) << std::endl;
    std::cout << HASH_IDX_DEFERRED_FREE_TAB(0xffb7) << std::endl;

    ThreadContextPtr context1 = (ThreadContextPtr) malloc(sizeof(ThreadContext));
    ThreadContextPtr context2 = (ThreadContextPtr) malloc(sizeof(ThreadContext));
    std::cout << "allocated Context: " << context1 << std::endl;
    std::cout << "allocated Context: " << context2 << std::endl;
   
    SetThreadContext((void*) 0x17b7, context1);
    SetThreadContext((void*) 0x3fb5, context2);


    std::cout << "Hash keys: " << HASH_IDX_THR_CONTEXT_TAB(context1 -> key) << ", "
                               << HASH_IDX_THR_CONTEXT_TAB(context2 -> key) << ", "
                               << HASH_IDX_THR_CONTEXT_TAB((void*) 0x3fbf) << std::endl;

   std::cout << "Removed Context: " << RemoveThreadContext((void*) 0x17b7) << std::endl;
   std::cout << "Removed Context: " << RemoveThreadContext((void*) 0x3fb5) << std::endl;
 
   SetThreadContext((void*) 0x17b7, context1);
   SetThreadContext((void*) 0x3fb5, context2);
   
   UpdateThreadContext((void*) 0x3fb5, (void*) 0x3fbf);

   std::cout << "Removed Context: " << RemoveThreadContext((void*) 0x17b7) << std::endl;
   std::cout << "Removed Context: " << RemoveThreadContext((void*) 0x3fb5) << std::endl;
   std::cout << "Removed Context: " << RemoveThreadContext((void*) 0x3fbf) << std::endl;

   return 0;
}
#endif
