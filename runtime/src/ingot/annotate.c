/*
 * annotate.c
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

#include "annotate.h"
#include <assert.h>

size_t get_size_for_type(int type)
{
    switch(type) {
    case PT_dict: return sizeof(dict);
    case PT_dictEntry: return sizeof(dictEntry);
    case PT_dictEntryPtr: return sizeof(dictEntry *);
    case PT_sds5: return sizeof(struct sdshdr5);
    case PT_sds8: return sizeof(struct sdshdr8);
    case PT_sds16: return sizeof(struct sdshdr16);
    case PT_sds32: return sizeof(struct sdshdr32);
    case PT_sds64: return sizeof(struct sdshdr64);
    case PT_robj: return sizeof(robj);
    case PT_serverRoot: return sizeof(struct serverRoot);
    default: assert(0);
    }
    return -1;
}

size_t PT_dict_offset[_field_dict_COUNT];
size_t PT_dict_size[_field_dict_COUNT];

void init_PT_dict_tables()
{
    PT_dict_offset[_field_dict_type] = offsetof(dict, type);
    PT_dict_offset[_field_dict_privdata] = offsetof(dict, privdata);
    PT_dict_offset[_field_dict_ht_table] = offsetof(dict, ht[0].table);
    PT_dict_offset[_field_dict_ht_size] = offsetof(dict, ht[0].size);
    PT_dict_offset[_field_dict_ht_sizemask] = offsetof(dict, ht[0].sizemask);
    PT_dict_offset[_field_dict_ht_used] = offsetof(dict, ht[0].used);
    PT_dict_offset[_field_dict_rehashidx] = offsetof(dict, rehashidx);
    PT_dict_offset[_field_dict_iterators] = offsetof(dict, iterators);
    PT_dict_offset[_field_dict_persist] = offsetof(dict, persist);

    PT_dict_size[_field_dict_type ] = sizeof(dictType *);
    PT_dict_size[_field_dict_privdata ] = sizeof(void *);
    PT_dict_size[_field_dict_ht_table ] = sizeof(dictEntry**);
    PT_dict_size[_field_dict_ht_size ] = sizeof(unsigned long);
    PT_dict_size[_field_dict_ht_sizemask ] = sizeof(unsigned long);
    PT_dict_size[_field_dict_ht_used ] = sizeof(unsigned long);
    PT_dict_size[_field_dict_rehashidx ] = sizeof(long);
    PT_dict_size[_field_dict_iterators ] = sizeof(unsigned long);
    PT_dict_size[_field_dict_persist ] = sizeof(int);
}

size_t PT_dictEntry_offset[_field_dictEntry_COUNT];
size_t PT_dictEntry_size[_field_dictEntry_COUNT];

void init_PT_dictEntry_tables()
{
    PT_dictEntry_offset[_field_dictEntry_key] = offsetof(dictEntry, key);
    PT_dictEntry_offset[_field_dictEntry_v] = offsetof(dictEntry, v);
    PT_dictEntry_offset[_field_dictEntry_next] = offsetof(dictEntry, next);

    PT_dictEntry_size[_field_dictEntry_key] = sizeof(void*);
    PT_dictEntry_size[_field_dictEntry_v] = sizeof(void*);
    PT_dictEntry_size[_field_dictEntry_next] = sizeof(struct dictEntry*);    
}

size_t PT_robj_offset[_field_robj_COUNT];
size_t PT_robj_size[_field_robj_COUNT];

void init_PT_robj_tables()
{
    PT_robj_offset[_field_robj_type] = 0;
    PT_robj_offset[_field_robj_lru] = 1;
    PT_robj_offset[_field_robj_refcount] = offsetof(robj, refcount);
    PT_robj_offset[_field_robj_ptr] = offsetof(robj, ptr);

    PT_robj_size[_field_robj_type] = 1;
    PT_robj_size[_field_robj_lru] = 3;
    PT_robj_size[_field_robj_refcount] = sizeof(int);
    PT_robj_size[_field_robj_ptr] = sizeof(void*);
}

size_t PT_sdshdr5_offset[_field_sdshdr5_COUNT];
size_t PT_sdshdr5_size[_field_sdshdr5_COUNT];

void init_PT_sdshdr5_tables() {
    PT_sdshdr5_offset[_field_sdshdr5_b_size] = 0;
    PT_sdshdr5_offset[_field_sdshdr5_str] = offsetof(sdshdr5,str);

    PT_sdshdr5_size[_field_sdshdr5_b_size] = 1;
    PT_sdshdr5_size[_field_sdshdr5_str] = 1;
}

size_t PT_sdshdr8_offset[_field_sdshdr8_COUNT];
size_t PT_sdshdr8_size[_field_sdshdr8_COUNT];

void init_PT_sdshdr8_tables() {
    PT_sdshdr8_offset[_field_sdshdr8_len] = offsetof(sdshdr8, len);
    PT_sdshdr8_offset[_field_sdshdr8_alloc] = offsetof(sdshdr8, alloc);
    PT_sdshdr8_offset[_field_sdshdr8_b_size] = offsetof(sdshdr8, b);
    PT_sdshdr8_offset[_field_sdshdr8_str] = offsetof(sdshdr8, str);

    PT_sdshdr8_size[_field_sdshdr8_len] = sizeof(uint8_t);
    PT_sdshdr8_size[_field_sdshdr8_alloc] = sizeof(uint8_t);
    PT_sdshdr8_size[_field_sdshdr8_b_size] = 1;
    PT_sdshdr8_size[_field_sdshdr8_str] = 1;
}

size_t PT_sdshdr16_offset[_field_sdshdr16_COUNT];
size_t PT_sdshdr16_size[_field_sdshdr16_COUNT];

void init_PT_sdshdr16_tables() {
    PT_sdshdr16_offset[_field_sdshdr16_len] = offsetof(sdshdr16, len);
    PT_sdshdr16_offset[_field_sdshdr16_alloc] = offsetof(sdshdr16, alloc);
    PT_sdshdr16_offset[_field_sdshdr16_b_size] = offsetof(sdshdr16, b);
    PT_sdshdr16_offset[_field_sdshdr16_str] = offsetof(sdshdr16, str);

    PT_sdshdr16_size[_field_sdshdr16_len] = sizeof(uint16_t);
    PT_sdshdr16_size[_field_sdshdr16_alloc] = sizeof(uint16_t);
    PT_sdshdr16_size[_field_sdshdr16_b_size] = 1;
    PT_sdshdr16_size[_field_sdshdr16_str] = 1;
}

size_t PT_sdshdr32_offset[_field_sdshdr32_COUNT];
size_t PT_sdshdr32_size[_field_sdshdr32_COUNT];

void init_PT_sdshdr32_tables() {
    PT_sdshdr32_offset[_field_sdshdr32_len] = offsetof(sdshdr32, len);
    PT_sdshdr32_offset[_field_sdshdr32_alloc] = offsetof(sdshdr32, alloc);
    PT_sdshdr32_offset[_field_sdshdr32_b_size] = offsetof(sdshdr32, b);
    PT_sdshdr32_offset[_field_sdshdr32_str] = offsetof(sdshdr32, str);

    PT_sdshdr32_size[_field_sdshdr32_len] = sizeof(uint32_t);
    PT_sdshdr32_size[_field_sdshdr32_alloc] = sizeof(uint32_t);
    PT_sdshdr32_size[_field_sdshdr32_b_size] = 1;
    PT_sdshdr32_size[_field_sdshdr32_str] = 1;
}

size_t PT_sdshdr64_offset[_field_sdshdr64_COUNT];
size_t PT_sdshdr64_size[_field_sdshdr64_COUNT];

void init_PT_sdshdr64_tables() {
    PT_sdshdr64_offset[_field_sdshdr64_len] = offsetof(sdshdr64, len);
    PT_sdshdr64_offset[_field_sdshdr64_alloc] = offsetof(sdshdr64, alloc);
    PT_sdshdr64_offset[_field_sdshdr64_b_size] = offsetof(sdshdr64, b);
    PT_sdshdr64_offset[_field_sdshdr64_str] = offsetof(sdshdr64, str);

    PT_sdshdr64_size[_field_sdshdr64_len] = sizeof(uint64_t);
    PT_sdshdr64_size[_field_sdshdr64_alloc] = sizeof(uint64_t);
    PT_sdshdr64_size[_field_sdshdr64_b_size] = 1;
    PT_sdshdr64_size[_field_sdshdr64_str] = 1;
}


int offset_to_field(int type /* PersistType */, size_t offset)
{
    int i = 0;
    int idx = 0;
    switch(type) {
    case PT_dict:

        if(offset >= PT_dict_offset[_field_dict_ht_table] &&
           offset < PT_dict_offset[_field_dict_rehashidx]) {
            /* Need to encode ht index if any, at this point */
            idx = (offset - PT_dict_offset[_field_dict_ht_table]) / sizeof(dictht);
            offset = offset - idx * sizeof(dictht);
        }

        while(i < _field_dict_COUNT && PT_dict_offset[i] + PT_dict_size[i] <= offset) i++;

        return idx << 8 | i;
        
    case PT_dictEntry:
        while(i < _field_dictEntry_COUNT && PT_dictEntry_offset[i] + PT_dictEntry_size[i] <= offset) i++;
        return i;
        
    case PT_dictEntryPtr:
        return 0;
        
    case PT_sds5:
        if(offset == 0) return _field_sdshdr5_b_size;
        idx = offset - 1;
        return idx << 8 | _field_sdshdr5_str;
        
    case PT_sds8:
        while(i <_field_sdshdr8_COUNT && PT_sdshdr8_offset[i]+PT_sdshdr8_size[i] <= offset) i++;
        if(i >= _field_sdshdr8_str) {
            idx = offset - PT_sdshdr8_offset[_field_sdshdr8_str];
        }
        return idx << 8 | i;
        
    case PT_sds16:
        while(i <_field_sdshdr16_COUNT && PT_sdshdr16_offset[i]+PT_sdshdr16_size[i] <= offset) i++;
        if(i >= _field_sdshdr16_str) {
            idx = offset - PT_sdshdr16_offset[_field_sdshdr16_str];
        }
        return idx << 8 | i;
        
    case PT_sds32:
        while(i <_field_sdshdr32_COUNT && PT_sdshdr32_offset[i]+PT_sdshdr32_size[i] <= offset) i++;
        if(i >= _field_sdshdr32_str) {
            idx = offset - PT_sdshdr32_offset[_field_sdshdr32_str];
        }
        return idx << 8 | i;
        
    case PT_sds64:
        while(i <_field_sdshdr64_COUNT && PT_sdshdr64_offset[i]+PT_sdshdr64_size[i] <= offset) i++;
        if(i >= _field_sdshdr64_str) {
            idx = offset - PT_sdshdr64_offset[_field_sdshdr64_str];
        }
        return idx << 8 | i;
        
    case PT_robj:
        while(i < _field_robj_COUNT && PT_robj_offset[i]+PT_robj_size[i] <= offset) i++;
        return i;
        
    default: return offset;
    }
}

size_t offset_of_field(int type, int field)
{
    int idx;
    size_t base_offset;
    
    switch(type) {
    case PT_dict:
        idx = field >> 8;
        field = field & 0xFF;
        assert(field < _field_dict_COUNT);
        base_offset = PT_dict_offset[field];
        return base_offset + idx * sizeof(dictht);
        break;
        
    case PT_dictEntry:
        assert(field < _field_dictEntry_COUNT);
        return PT_dictEntry_offset[field];
        break;
        
    case PT_dictEntryPtr:
        return 0;
        break;

    case PT_sds5:
        idx = (field >> 8);
        field = field & 0xFF;
        assert(field < _field_sdshdr5_COUNT);
        return PT_sdshdr5_offset[field] + idx * sizeof(char);
        
    case PT_sds8:
        idx = (field >> 8);
        field = field & 0xFF;
        assert(field < _field_sdshdr8_COUNT);
        return PT_sdshdr8_offset[field] + idx * sizeof(char);
        
    case PT_sds16:
        idx = (field >> 8);
        field = field & 0xFF;
        assert(field < _field_sdshdr16_COUNT);
        return PT_sdshdr16_offset[field] + idx * sizeof(char);
        
    case PT_sds32:
        idx = (field >> 8);
        field = field & 0xFF;
        assert(field < _field_sdshdr32_COUNT);
        return PT_sdshdr32_offset[field] + idx * sizeof(char);
        
    case PT_sds64:
        idx = (field >> 8);
        field = field & 0xFF;
        assert(field < _field_sdshdr64_COUNT);
        return PT_sdshdr64_offset[field] + idx * sizeof(char);
        
    case PT_robj:
        assert(field < _field_robj_COUNT);
        return PT_robj_offset[field];
        break;
        
    default: return field;
    }
}

size_t size_of_field(int type, int field) {

    field = field & 0xFF;
    switch(type) {
    case PT_dict:
        assert(field < _field_dict_COUNT);
        return PT_dict_size[field];
    case PT_dictEntry:
        assert(field < _field_dictEntry_COUNT);
        return PT_dictEntry_size[field];
    case PT_dictEntryPtr:
        assert(field == 0);
        return sizeof(dictEntry*);
    case PT_sds5:
        return PT_sdshdr5_size[field];
    case PT_sds8:
        return PT_sdshdr8_size[field];
    case PT_sds16:
        return PT_sdshdr16_size[field];
    case PT_sds32:
        return PT_sdshdr32_size[field];
    case PT_sds64:
        return PT_sdshdr64_size[field];
    case PT_robj:
        return PT_robj_size[field];
    default: assert(0 && "Bad type passed to size_of_field");
        return 0;
    }
}

void init_translation_tables() {
    init_PT_dict_tables();
    init_PT_dictEntry_tables();
    init_PT_sdshdr5_tables();
    init_PT_sdshdr8_tables();
    init_PT_sdshdr16_tables();
    init_PT_sdshdr32_tables();
    init_PT_sdshdr64_tables();
    init_PT_robj_tables();
}

void generate_diff_record(container * c,
                          int field_id,
                          int index,
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
        process_diff_dictEntry(c,field_id,old_addr,new_addr);
        break;
    case PT_dictEntryPtr:
        process_diff_dictEntryPtr(c,field_id,index, old_addr, new_addr);
        break;
    default:
        break;
    } //end switch
}
