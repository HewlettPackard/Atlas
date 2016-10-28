/*
 * persist.c
 *
 * Author: Daniel Fryer
 *
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

#include "ingot.h"
#include "annotate.h"

#include <Judy.h>
#include <atlas_alloc.h>
#include <atlas_api.h>
#include <atlas_log.h>
#include <assert.h>

/*
 * This innocuous void * is actually where our Judy array lives
 */

void * md;
void * md_free;
void * md_new;

/**
 * Functions for getting info about persistent regions
 **/

/* The one true persistent region */
int pr;
FILE* messages;

void * pr_base;
void * pr_limit;
//#define PRLOG(...) fprintf(my_fd, __VA_ARGS__)
#define PRLOG(...)

//#define SUPPRESS_MD

uintptr_t get_region_base(int region) {
    return (uintptr_t)NVM_GetRegionBaseAddr((uint32_t)region);
}

void set_pr(int persistRegion)
{
    pr = persistRegion;
    messages = fopen("persist.log","a");
    setbuf(messages, NULL);
    PRLOG("Set persist region %d\n", persistRegion);
    pr_base = get_region_base(pr);
    pr_limit = pr_base + (1L<<32);
    init_translation_tables();
}

uintptr_t get_base(void * address)
{
    intptr_t base;
    if(address == 0) return 0;
    
    base = (intptr_t)address -
        get_region_base(persistent_region(address));
   
    
    return (uintptr_t)base >> 3;
}

int persistent_region(void* ptr)
{
    /*
    if(NVM_IsInOpenPR(ptr,1)) {
        return NVM_GetRegionForAddr(ptr);
    }
    */
    if(ptr >= pr_base && ptr < pr_limit) {
        return pr;
    }
    return -1;
}

int same_region(void* ptr1, void* ptr2) {
    return persistent_region(ptr1) == persistent_region(ptr2);
}

/**
 * Functions for interpreting addresses and types
 */

int get_allocation_type_i(struct allocation_record * ar)
{
    return (int)ar->type | ((ar->flags & AR_Array) ? PT_ARRAY : 0);
}
int get_allocation_type(void * address, void * metadata) {
    uintptr_t base;

    struct allocation_record * value;

    base = get_base(address);
    JLG(value, metadata, base);
    if(!value) return 0;
    
    return get_allocation_type_i(value);
}

void * get_parent_i(int pregion, struct allocation_record * ar)
{
    uintptr_t parent_base;
    
    parent_base = ((uintptr_t)ar->parent_hi << 32
                   | (uintptr_t)ar->parent_lo);
    
    if(parent_base == 0) return 0;
    
    return (void*)(get_region_base(pregion)
                   + (parent_base << 3));
}

void * get_parent(void * address, void * metadata)
{
    uintptr_t base;
    struct allocation_record * value;

    if(address == 0) return 0;
   
    base = get_base(address);
    JLG(value, metadata, base);
    if(!value) return 0;
    
    return get_parent_i(persistent_region(address), value);
}

size_t get_container_size_i(struct allocation_record * ar,
                            struct extended_record *ext)
{
    if(HAS_EXTENSION(ar->flags)) {
        int count;
        count = ((size_t)ext->size_hi << 32) | ext->size_lo;
        if (ar->flags & AR_Array) {
            return count * get_size_for_type(ar->type);
        } else {
            assert(ar->flags & AR_VarSize);
            return count;
        }
    } else {
        if(ar->flags & AR_VarSize) {
            assert(ar->flags & AR_LessThan16);
            return ar->count;
        }
        if(ar->flags & AR_Array) {
            assert(ar->flags & AR_LessThan16);
            return ar->count * get_size_for_type(ar->type);
        }
        return get_size_for_type(ar->type);
    }
}

size_t get_container_size(void * address, void * metadata)
{
    uintptr_t base;
    struct allocation_record * ar = 0;
    struct extended_record * ext = 0;
    
    base = get_base(address);
    /* Note - modifies value and base */
    JLG(ar, metadata, base);
    if(!ar) return 0;
    
    if(HAS_EXTENSION(ar->flags)) {
        JLG(ext, metadata, base+1);
    }
    
    return get_container_size_i(ar, ext);
}

/* Given an address, find the address of the containing allocation */
void * get_container(void * address, void * metadata) {
    uintptr_t base;
    struct allocation_record * value;
    struct extended_record *ext;
    void * container_address;
    size_t size;

    ext = 0;
    value = 0;
    base = get_base(address);
    
    /* Note - modifies value and base */
    JLL(value, metadata, base);
    if(!value) return 0;
    if(value->flags & AR_Extended) {
        ext = (struct extended_record*)value;
        JLP(value, metadata, base);
    } else if(HAS_EXTENSION(value->flags)) {
        JLG(ext, metadata, base+1);
    }
    
    assert(value && !(value->flags & AR_Extended));
    container_address = (void*) (get_region_base(persistent_region(address))
                                 + (base << 3));
    size = get_container_size_i(value, ext);
    
    /* Null if it's out of bounds */
    if((intptr_t)container_address + size <= (intptr_t)address) {
        return 0;
    }
    /* Ok, it's within size, return container */
    return container_address;
}

/**
 * Functions for allocating and deallocating typed persistent memory
 */

void insert_allocation_metadata(void * address,
                                int type,
                                size_t size,
                                void * parent)
{
    struct allocation_record * value;
    int is_array;
    uintptr_t base;
    uintptr_t parent_base;
    int res;

    base = get_base(address);

    /* First, record that we created something here */
    J1S(res, md_new, base);
    
    parent_base = get_base(parent);
    is_array = type & PT_ARRAY;
    type = type & ~PT_ARRAY;
    assert(type < 0xFFFF);
    PRLOG("Insert: %p %d %ld %p%s\n",
          address,
          type,
          size,
          parent,
          is_array ? " (array)":"");
    
    JLI(value, md, base);
    value->parent_lo = parent_base & 0xFFFFFFFF;
    value->parent_hi = (parent_base >> 32) & 0xFF;
    value->type = (short)(type);
    value->flags = 0;
    
    /*
     * Set the flags to deal with variable size objects or arrays
     * and insert an extended record to contain the size if necessary 
     */
    
    if(is_array) {
        value->flags |= AR_Array;
        assert(size % get_size_for_type(type) == 0);
        size_t count = size / get_size_for_type(type);
        if(count < 16) {
            value->flags |= AR_LessThan16;
            value->count = count;
        } else {
            struct extended_record * ext;
            JLI(ext, md, base+1);
            ext->size_lo = (uint32_t)(count & 0xFFFFFFFF);
            ext->size_hi = (uint16_t)((count >> 32) & 0xFFFF);
            ext->flags = AR_Extended;
        }
            
    } else if(size > get_size_for_type(type)) {
        value->flags |= AR_VarSize;
        if(size < 16) {
            value->flags |= AR_LessThan16;
            value->count = size;
        } else {
            struct extended_record * ext;
            JLI(ext, md, base+1);
            ext->size_lo = (uint32_t)(size & 0xFFFFFFFF);
            ext->size_hi = (uint16_t)((size >> 32) & 0xFFFF);
            ext->flags = AR_Extended;
        }
    } 
}

void delete_allocation_metadata(void * address)
{
    int res;
    uintptr_t base;
    struct allocation_record * mdPtr;
    struct allocation_record * mdFreePtr;

    PRLOG("Delete: %p\n",address);
    base = get_base(address);

    /* Clear the "new" bit if it was set */
    J1U(res, md_new, base);

    /* Get the metadata that is about to be deleted */
    JLG(mdPtr, md, base);
    assert(mdPtr);
    
    /* Move it to the array of deleted metadata */
    JLI(mdFreePtr, md_free, base);
    *mdFreePtr = *mdPtr;
    
    if(HAS_EXTENSION(mdPtr->flags)) {
        /* Overwrite mdPtr, don't need it anymore */
        JLG(mdPtr, md, base+1);
        JLI(mdFreePtr, md_free, base+1);
        *mdFreePtr = *mdPtr;
        JLD(res, md, base+1);
    }
    
    JLD(res, md, base);
}

void * persistent_alloc(size_t sz, PersistType type, int region)
{
    void * ptr;

    /* At this point, could decide based on type to
       allocate from pool */
    
    ptr = nvm_alloc(sz, region);
#ifndef SUPPRESS_MD
    /* We don't know the parent / backpointer yet */
    insert_allocation_metadata(ptr, type, sz, 0);
#endif
    /* Maybe we want to put this into a temporary created object array? */

    return ptr;
}

void * persistent_realloc(void * ptr, size_t newsize)
{
    void * newptr;
    int type;
    void * parent;
    
    newptr = nvm_realloc(ptr, newsize, persistent_region(ptr));
#ifndef SUPPRESS_MD
    /* Pointers may be the same but this avoids tricky logic to update
     * size and possibly add/remove an extension record */
    type = get_allocation_type(ptr, md);
    parent = get_parent(ptr, md);
    delete_allocation_metadata(ptr);
    if(ptr == newptr) {
        
        /* Keep the same parent */
        insert_allocation_metadata(newptr, type, newsize, parent);
        
    } else {
        
        /* We don't know where this pointer will be stored, wipe parent */
        insert_allocation_metadata(newptr, type, newsize, 0);
    }
#endif
    return newptr;
}

void persistent_free(void * ptr)
{
    /* Maybe generate change records here, before we scribble on it */
#ifndef SUPPRESS_MD
    delete_allocation_metadata(ptr);
#endif
    nvm_free(ptr);
}

/* called when parent points into persistent region and *parent = child */
void persistent_parent(void * parent, void * child)
{
#if 0
    uintptr_t base = get_base(child);
    uintptr_t parent_base = get_base(parent);
    struct allocation_record * ar;

    JLG(ar, md, base);
    assert(ar);
    assert(parent_base < 0x0000008000000000UL);
    ar->parent_hi = parent_base & 0xFFFFFFFF;
    ar->parent_lo = (parent_base >> 32) & 0xFF;
#endif
    (void) parent;
    (void) child;
    PRLOG("PParent: %p %p\n",parent,child);
}

int search_new_objects(void * address)
{
    uintptr_t base = get_base(address);
    int res;
    J1T(res, md_new, base);
    return res;
}

/****************************************************************
 *
 * Transaction processing 
 *
 ***************************************************************/

/* 
 * The metadata diff loop thing
 */
void diff_one_log_entry(AtlasLogEntry * log);

void diff_deleted_container(void * address,
                                int type,
                                size_t size,
                                void * parent);

void * unique_ptrs;
void * other_ptrs;



void unique_ptr_set(void* ptr)
{
    uint64_t * value;
    if(!ptr) return;
    JLI(value, unique_ptrs, (intptr_t)ptr);
    // assert(*value != UNIQUE_SET);
    if(*value == UNIQUE_CLEAR) *value = 0;
    else *value = UNIQUE_SET;

}

void unique_ptr_clear(void* ptr)
{
    uint64_t * value;
    if(!ptr) return;
    JLI(value, unique_ptrs, (intptr_t)ptr);
    //hack, we allow multiple updates as long as they're interleaved
    //in log order :-/
    // assert(*value != UNIQUE_CLEAR);
    if(*value == UNIQUE_SET) *value = 0;
    else *value = UNIQUE_CLEAR;
}
    
void process_committing_transaction()
{
    AtlasLogEntry *start, *cur, *end;
    int res;
    void * value;
    uintptr_t deleteIdx;

#ifdef SUPPRESS_MD
    return;
#endif

#ifdef SUPPRESS_CHECK
    return;
#endif

    nvm_get_log(&start, &end);
    cur = start;
    do {
        if(!cur) {
            /* Freak out! */
        }
        switch(cur->Type) {
        case LE_str:
        case LE_memset:
        case LE_memcpy:
        case LE_memmove:
        case LE_strcpy:
        case LE_strcat:
            diff_one_log_entry(cur);
            break;
        case LE_alloc:
        case LE_free:
        default: break;
        }

        cur = cur->Next;
    } while(cur != end->Next);

    /* Process remaining deletes */
    deleteIdx = 0;
    JLF(value, md_free, deleteIdx);
    while(value != 0) {
        struct allocation_record * ar = value;
        struct extended_record * ext = 0;
        int region = pr; /* Limit ourselves to One True Region */
        size_t size;
        int type;
        void * parent;
        void * address = (void*)(get_region_base(region) + (deleteIdx << 3));
        
        if(HAS_EXTENSION(ar->flags)) {
            JLN(ext, md_free, deleteIdx);
        }
        
        size = get_container_size_i(ar,ext);
        parent = get_parent_i(region, ar);
        type = get_allocation_type_i(ar);
        diff_deleted_container(address, type, size, parent);
        
        JLN(value, md_free, deleteIdx);
    }
    
    /* Post-diff invariant processing */
    check_deferred_invariants();
    
    /* Nuke the md_free array! */
    JLFA(res, md_free);
    J1FA(res, md_new);
    JLFA(res, unique_ptrs);
}
    


/* 

 This version ONLY WORKS if the deleted region hasn't been trampled on.
 If this is a problem, we can trigger diffing at the time of delete, if
 the object was not created during this same transaction.
 
*/
void diff_deleted_container(void * address,
                            int type,
                            size_t size,
                            void * parent)
{
    container delete_container;
    int field_id;
    size_t field_size;
    size_t field_start;
    void * new_field_address;
    void * old_field_address;
    unsigned int i, index, cur_offset;
    
    
    delete_container.start = address;
    delete_container.end = delete_container.start + size;
    delete_container.state = CONTAINER_STATE_DELETE;
    delete_container.type = type & ~PT_ARRAY;
    delete_container.isArray = (type & PT_ARRAY) > 0;

    i = 0;
    index = 0;
    /* Iterate over all fields */
    while(i < size) {
        cur_offset = i - index * get_size_for_type(delete_container.type);
        field_id = offset_to_field(delete_container.type, cur_offset);
        field_size = size_of_field(delete_container.type, field_id);
        
        new_field_address = 0;
        old_field_address = delete_container.start + i;

        /* Make sure at least that the whole field made it into this
         * log entry. */
        assert(i + field_size <= size);
        generate_diff_record(&delete_container,
                             field_id,
                             index,
                             old_field_address,
                             new_field_address,
                             field_size);
        
        /* Move to next field */
        i = i + field_size;
        if(delete_container.isArray) {
            index = i % get_size_for_type(delete_container.type);
        }
    }
}

/* Currently doesn't handle deletes, eep */
void diff_one_log_entry(AtlasLogEntry * log)
{
    char * oldPtr;
    char * newPtr;
    int i;
    container last_container;
    int field_id;
    size_t field_size;
    size_t field_start;
    int index;
    void * new_field_address;
    void * old_field_address;
    int sz; //Number of bytes in this logentry
    intptr_t effective_offset;

    if(persistent_region(log->Addr) != 1) return;

    if(log->Type == LE_str) {
        /* log->Size is inexplicably measured in *bits* */
        sz = log->Size / 8;
    } else {
        /* For memcpy, strcpy it is measured in bytes */
        sz = log->Size;
    }
    
    if(sz <= 8 && log->Type == LE_str) {
        oldPtr = (char *)&log->ValueOrPtr;
    } else {
        oldPtr = (char *)log->ValueOrPtr;
    }

    newPtr = log->Addr;
    memset(&last_container, 0, sizeof(last_container));
    index = 0;
    i = 0;
    while(i < sz) {
        if(oldPtr[i] == newPtr[i]) {
            /* Boring, skip */
            i++;
            continue;
        }
        
        if(!last_container.state ||
           (newPtr+i) >= last_container.end ||
           (newPtr+i) < last_container.start) {
            
            last_container.start = get_container(newPtr+i, md);
            if(last_container.start == 0) {
                /* This is some deleted region or internal metadata, skip to start of next word.
                 * Since log entry should be word-aligned, we want i to be
                 * the next multiple of 8 */
                i = (i & ~0x7) + 8;
                continue;
            }
            
            /* Yo what if it's an array? */
            last_container.type = get_allocation_type(last_container.start, md);
            last_container.isArray = (last_container.type & PT_ARRAY) > 0;
            
            last_container.type = last_container.type & ~PT_ARRAY;
                        
            last_container.end = last_container.start +
                get_container_size(last_container.start, md);

            last_container.state = 0;
            index = 0;
            
            if(search_new_objects(last_container.start)) {
                last_container.state = CONTAINER_STATE_CREATE;
                /* there is a chance we could be hovering over a deleted
                   object if we allow reuse
                */
            } else {
                last_container.state = CONTAINER_STATE_UPDATE;
            }
               
        }    

        effective_offset =  (newPtr+i) - last_container.start;
        if(last_container.isArray) {
            index = effective_offset / get_size_for_type(last_container.type);
        }
        field_id = offset_to_field(last_container.type,
                                   effective_offset - index
                                   * get_size_for_type(last_container.type));
        
        /* But we might be a few bytes into the field */
        field_start = offset_of_field(last_container.type, field_id);
        field_size = size_of_field(last_container.type, field_id);
        new_field_address = last_container.start + field_start
            + index * get_size_for_type(last_container.type);
        
        old_field_address = oldPtr + ((char*)new_field_address - newPtr);

        /* Make sure at least that the whole field made it into this
         * log entry. */
        assert((intptr_t)new_field_address - (intptr_t)newPtr + (int)field_size <= sz);
        generate_diff_record(&last_container,
                             field_id,
                             index,
                             old_field_address,
                             new_field_address,
                             field_size);
        
        /* Move to next field */
        i = (intptr_t)new_field_address - (intptr_t)newPtr + field_size;
       
    }
}

