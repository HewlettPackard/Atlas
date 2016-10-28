/*
 * check_txn.c
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
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <atlas_log.h>

#include "ingot.h"
#include "annotate.h"
#include "Judy.h"

/* 
 * The metadata diff loop thing
 */
void diff_one_log_entry(AtlasLogEntry * log);

void * unique_ptrs;
void * other_ptrs;

enum {
    UNIQUE_SET = 1,
    UNIQUE_CLEAR = 2
};

void unique_ptr_set(void* ptr)
{
    long * value;
    JLI(value, unique_ptrs, (intptr_t)ptr);
    assert((*value & UNIQUE_SET) == 0);
    *value |= UNIQUE_SET;
}

void unique_ptr_clear(void* ptr)
{
    long * value;
    JLI(value, unique_ptrs, (intptr_t)ptr);
    assert((*value & UNIQUE_CLEAR) == 0);
    *value |= UNIQUE_CLEAR;
}
    
void process_committing_transaction()
{
    AtlasLogEntry *start, *cur, *end;
    int res;
    void * value;
    uintptr_t deleteIdx;

    init_translation_tables();
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
        size_t size;
        int type;
        void * parent;
        
        if(HAS_EXTENSION(ar->flags)) {
            JLN(ext, md_free, deleteIdx);
        }

        size = get_container_size_i(ar,ext);
        parent = get_parent_i(persistent_region(address), ar);
        diff_deleted_container(address, ar->type, size, parent);
        
        JLN(value, md_free, deleteIdx);
    }
    /* Post-diff invariant processing */

    
    /* Nuke the md_free array! */
    JLFA(res, md_free);
    J1FA(res, md_new);
    JLFA(res, unique_ptrs);
}

void generate_diff_record(container * c,
                          int field_id,
                          void* old_addr,
                          void* new_addr,
                          size_t field_size);

/* Currently doesn't handle deletes, eep */
void diff_one_log_entry(AtlasLogEntry * log)
{
    char * oldPtr;
    char * newPtr;
    int i;

    void * old_type_info;
    void * new_type_info;

    container last_container;
    container delete_container;
    int field_id;
    size_t field_size;
    size_t field_start;
    void * new_field_address;
    void * old_field_address;
    int sz; //Number of bytes in this logentry

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
    last_container.state = 0;
    last_container.start = 0;
    last_container.end = 0;
    
    /* 
     * Oh no! We can look up its new type in the current md array,
     * but what about its old type??
     * This is a problem.
     */
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
            if(last_container.type & PT_ARRAY) {
                /* Can't handle arrays right now */
                i++;
                continue;
            }
            
            last_container.end = last_container.start +
                get_container_size(last_container.start, md);

            last_container.state = 0;
            if(search_new_objects(last_container.start)) {
                last_container.state = CONTAINER_STATE_CREATE;
                /* there is a chance we could be hovering over a deleted
                   object
                */
            } else {
                last_container.state = CONTAINER_STATE_CHANGE;
            }
               
        }    
        
        field_id = offset_to_field(last_container.type,
                                   (newPtr+i)-last_container.start);
        
        /* But we might be a few bytes into the field */
        field_start = offset_of_field(last_container.type, field_id);
        field_size = size_of_field(last_container.type, field_id);
        new_field_address = last_container.start + field_start;
        old_field_address = oldPtr + ((char*)new_field_address - newPtr);

        /* Make sure at least that the whole field made it into this
         * log entry. */
        assert((intptr_t)new_field_address - (intptr_t)newPtr + field_size <= sz);
        generate_diff_record(&last_container,
                             field_id,
                             old_field_address,
                             new_field_address,
                             field_size);
        
        /* Move to next field */
        i = (intptr_t)new_field_address - (intptr_t)newPtr + field_size;
    }
}

void generate_diff_record(container * c,
                          int field_id,
                          void* old_addr,
                          void* new_addr,
                          size_t field_size)
{
    (void)field_size;
    
    switch(c->type) {
    case PT_dict: 
        process_diff_dict(c,field_id,old_addr,new_addr);
        break;
        
    case PT_dictEntry:
        process_diff_dict(c,field_id_old_addr,new_addr);
        break;
        
    default:
        break;
    } //end switch
}
