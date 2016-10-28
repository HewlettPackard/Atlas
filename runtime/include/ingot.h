/*
 * ingot.h
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
 

#ifndef SRC_PERSIST_H_
#define SRC_PERSIST_H_

#include <stdlib.h>
#include <stdint.h>

#define PT_ARRAY 0x80000000

#define CONTAINER_STATE_CREATE 1
#define CONTAINER_STATE_DELETE 2
#define CONTAINER_STATE_UPDATE 3

typedef struct {
    PersistType type;
    int isArray;
    int index;
    char* start;
    char* end;
    int state; 
} container;

extern void * md;
extern void * md_free;
extern void * md_new;

int persistent_region(void* ptr);

int same_region(void* ptr1, void* ptr2);

void * persistent_alloc(size_t sz, PersistType type, int region);
void * persistent_realloc(void * ptr, size_t newsize);

void persistent_free(void * ptr);

/* called when parent points into persistent region and *parent = child */
void persistent_parent(void * parent, void * child);

void set_pr(int persistRegion);

/* Metadata access API */
size_t get_size_for_type(int type);
int get_allocation_type(void * address, void * metadata);
void * get_parent(void * address, void * metadata);
size_t get_container_size(void * address, void * metadata);
void * get_container(void * address, void * metadata);
int search_new_objects(void * address);
uintptr_t get_base(void * address);

/* check_txn.c, called on commmit to do diffs & checking */
void process_committing_transaction();

enum {
    UNIQUE_SET = 1,
    UNIQUE_CLEAR = 2
};
extern void * unique_ptrs;
void unique_ptr_set(void* ptr);
void unique_ptr_clear(void* ptr);

/* Metadata internals used by the transaction checker */
struct allocation_record {
    uint32_t parent_lo;
    uint16_t type;
    uint8_t parent_hi;
    uint8_t count:4;
    uint8_t flags:4;
};

struct extended_record {
    uint32_t size_lo;
    uint16_t size_hi;
    uint8_t padding1;
    uint8_t padding2:4;
    uint8_t flags:4;
};

enum AllocationRecordFlags {
    AR_Extended = 1, /* This is an extended record */
    AR_Array = 2,
    AR_LessThan16 = 4,
    AR_VarSize = 8
};

/* We compare instead of masking because other flag bits should be zero!! */
#define HAS_EXTENSION(flags) ((flags) == AR_Array | (flags) == AR_VarSize)

#endif /* SRC_PERSIST_H_ */
