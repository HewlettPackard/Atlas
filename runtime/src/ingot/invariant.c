/*
 * invariant.c
 * 
 * Author: Daniel Fryer
 *
 * Invariant checks for Redis data structures
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
 *
 */

#include "ingot.h"
#include "annotate.h"

#include <assert.h>
#include <string.h>
#include <Judy.h>

void process_diff_dict(container *c,
                       int field_id,
                       void * old_addr,
                       void* new_addr)
{
    dict * d = (dict *)c->start;
    int idx = field_id >> 8;
    if(c->state == CONTAINER_STATE_CREATE ||
       c->state == CONTAINER_STATE_UPDATE) {
        
        switch(field_id & 0xFF) {
        case _field_dict_type:
            /* IMMUTABLE */
            assert(c->state == CONTAINER_STATE_CREATE);
            break;
            
        case _field_dict_privdata:
            /* No invariants */
            break;
            
        case _field_dict_ht_table:
            /* Invariant 1 */
            assert(d->ht[idx].table == 0 ||
                   get_allocation_type(d->ht[idx].table, md) ==
                   (PT_dictEntryPtr | PT_ARRAY));
            
            /* Invariant 2 */
            /* UNIQUE */
            //   this means - create CR for table container, or a change/delete
            //   of previous parent, and no other update/create CRs of fields
            //   pointing to table.
            if(d->ht[idx].table != 0) {
                unique_ptr_set(d->ht[idx].table);
            }
            /* Invariant 3 */
            assert(d->ht[idx].size == 0 ||
                   d->ht[idx].size <= get_container_size(d->ht[idx].table, md));

            /* Invariant 4 */
            // Need to set up deferred check. If this is not a newly created
            // table we'd like to see proof that it's coming from the right
            // sort of place.
            
            /* Here's where the SetParent goes */
            /* if c->type is create or update and newval != 0 */
            /* Also need to unParent oldval if it's nonzero */
            persistent_parent(c->start, d->ht[idx].table);
            break;
            
        case _field_dict_ht_size:
            /* Invariant 3 */
            assert(d->ht[idx].size == 0 ||
                   d->ht[idx].size <= get_container_size(d->ht[idx].table, md));
            
            /* Invariant 4 */
            // SIZE changed; table probably changed (or there was a recycling),
            // either way we need to make sure that all indices are checked.
            // Since this is a heavyweight check, defer.
            
            break;
            
        case _field_dict_ht_sizemask:
            break;
            
        case _field_dict_ht_used:
            /* Invariant ??? */
            assert(d->ht[idx].used <= d->ht[idx].size);
            break;
            
        case _field_dict_rehashidx:
            break;
            
        case _field_dict_iterators:
            break;
            
        case _field_dict_persist:
            assert(d->persist == 1);
            break;
            
        default:
            assert(0 && "Bad field id passed to process_diff_dict");
        }
    } // ends checks for CREATE|UPDATE

    if(c->state == CONTAINER_STATE_UPDATE || c->state == CONTAINER_STATE_DELETE) {
        dictEntry** table_oldval;
        switch(field_id & 0xFF) {
        case _field_dict_type:
            /* IMMUTABLE */
            assert(c->state == CONTAINER_STATE_DELETE);
            break;
        case _field_dict_ht_table: 
            /* UNIQUE */
          
            memcpy(&table_oldval, old_addr, sizeof(table_oldval));
            unique_ptr_clear(table_oldval);
            /* un-parent the old value */
            break;
        default:
            break;
        }
    }
}

void process_diff_dictEntry(container *c,
                            int field_id,
                            void * old_addr,
                            void * new_addr)
{
    dictEntry * de = (dictEntry*)c->start;

    if(c->state == CONTAINER_STATE_CREATE ||
       c->state == CONTAINER_STATE_UPDATE) {
        
        switch(field_id) {
        case _field_dictEntry_key:
            /* IMMUTABLE */
            assert(c->state == CONTAINER_STATE_CREATE);
            /* Invariant 2 */
            /* UNIQUE */
            /* Should register canonical dictEntry creation event here
             * But actually, we can check its creation/deletion in other
             * md arrays. */
            if(de->key != 0) {
                unique_ptr_set(de->key);
            }
            /* Invariant 4 */
            assert(de->key != 0);
            /* Invariant 5 */
            /* Invariant 6 */
            /* When we find our logical parent we can check 6 */
            break;
            
        case _field_dictEntry_v:
            break;
            
        case _field_dictEntry_next:
            /* Invariant 1 */
            assert(de->next == 0 ||
                   (get_allocation_type(de->next, md) == PT_dictEntry));

            /* Invariant 2 */
            /* UNIQUE */
            unique_ptr_set(de->next);

            /* Invariant 4 */
            
            /* Invariant 5 */
            assert(de->next == 0 ||
                   (de->next->key != 0 &&
                    get_allocation_type(de->next->key, md)
                     == get_allocation_type(de->key, md))
                   );

            /* Should register as physical parent of next,
             * and/or set P_logical(next) = P_logical(this).
             * Note that we might not know *who* our phys/log parent
             * is yet!! */
           
            break;
            
        default: break; /* Or maybe assert */
        } //end switch
    }

    if(c->state == CONTAINER_STATE_UPDATE ||
       c->state == CONTAINER_STATE_DELETE) {
        void* key_oldval;
        dictEntry* next_oldval;
        switch(field_id) {
        case _field_dictEntry_key:
            memcpy(&key_oldval, old_addr, sizeof(key_oldval));
            /* IMMUTABLE */
            assert(c->state == CONTAINER_STATE_DELETE);
            /* UNIQUE */
            unique_ptr_clear(key_oldval);
            break;
        case _field_dictEntry_next:
            memcpy(&next_oldval, old_addr, sizeof(next_oldval));
            /* UNIQUE */
            unique_ptr_clear(next_oldval);
        default:
            break;
        }
    }
}

void process_diff_dictEntryPtr(container *c,
                               int field_id,
                               int index,
                               void * old_addr,
                               void * new_addr)
{
    dictEntry * dep;
    if(c->state == CONTAINER_STATE_CREATE ||
       c->state == CONTAINER_STATE_UPDATE) {
        dep = *(dictEntry **)new_addr;
        unique_ptr_set(dep);
        assert(dep == 0 || get_allocation_type(dep, md) == PT_dictEntry);

        
    }
    
    if(c->state == CONTAINER_STATE_DELETE ||
       c->state == CONTAINER_STATE_UPDATE) {
        dep = *(dictEntry **)old_addr;
        unique_ptr_clear(dep);
    }
}

void check_deferred_invariants()
{
    uint64_t * flagPtr;
    uintptr_t address;
    int res;
    int set_viol = 0;
    int clear_viol = 0;
    
    /* UNIQUE POINTERS */
    JLF(flagPtr, unique_ptrs, address);
    while(flagPtr != 0) {
        if(*flagPtr == 0 || *flagPtr == (UNIQUE_SET | UNIQUE_CLEAR)) {
            JLN(flagPtr, unique_ptrs, address);
            continue;
        }

        if(*flagPtr & UNIQUE_SET) {
            J1T(res, md_new, get_base(address));
            if(!res) set_viol++;
           
        }

        if(*flagPtr & UNIQUE_CLEAR) {
            uint64_t * value;
            JLG(value, md_free, get_base(address));
            if(!value) clear_viol++;
           
        }
        JLN(flagPtr, unique_ptrs, address);
    }
    //printf("Set viol %d clear viol %d\n", set_viol, clear_viol);

    /* TABLE HASHING */
    /* This gets triggered if a table pointer changed
     * and we can't show that it was really just a pointer shift
     * from ht[1] to ht[0] */
    /*
      for each (dict, table) to check
        if exists old_parent(table) and old_parent == dict and
          old_size == new_size
    */
}
