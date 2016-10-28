/**
 *
 * annotate.h - Interface between Ingot framework and Redis
 *
 * Portions may be derived from content copyright (c) 2009-2012, 
 * Salvatore Sanfilippo <antirez at gmail dot com>, and subject to the following
 * license:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "ingot.h"

#include <stdint.h>
#include <stddef.h>

/* Enumeration of persistent Redis data types */

typedef enum PersistType {
	PT_dict = 1,
	PT_dictEntry,
	PT_dictEntryPtr,
	PT_dictht, /* Not a real container */
	PT_sds, /* Not a real container */
	PT_sds5 = PT_sds,
	PT_sds8 = PT_sds+1,
	PT_sds16 = PT_sds+2,
	PT_sds32 = PT_sds+3,
	PT_sds64 = PT_sds+4,
        PT_robj,
        PT_serverRoot
} PersistType;

/* Redis data structures, adapted from redis source */

typedef struct sdsBase {
    char size:5;
    char type:3;
} sdsBase;

typedef struct sds {
    char buf; /* Even an empty string has at least one character */  
} sds;

typedef struct sdshdr5 {
    sdsBase b;
    sds str;
} sdshdr5;

typedef struct sdshdr8 {
    uint8_t len;
    uint8_t alloc; 
    sdsBase b;
    sds str;
} sdshdr8;
    
typedef struct sdshdr16 {
    uint16_t len;
    uint16_t alloc; 
    sdsBase b;
    sds str;
} sdshdr16;

typedef struct sdshdr32 {
    uint32_t len;
    uint32_t alloc; 
    sdsBase b;
    sds str;
} sdshdr32;

typedef struct sdshdr64 {
    uint64_t len;
    uint64_t alloc; 
    sdsBase b;
    sds str;
} sdshdr64;
  
typedef struct dictEntry {
    void *key;
    union {
        void *val;
        uint64_t u64;
        int64_t s64;
        double d;
    } v;
    struct dictEntry *next;
} dictEntry;

typedef struct dictType {
    unsigned int (*hashFunction)(const void *key);
    void *(*keyDup)(void *privdata, const void *key);
    void *(*valDup)(void *privdata, const void *obj);
    int (*keyCompare)(void *privdata, const void *key1, const void *key2);
    void (*keyDestructor)(void *privdata, void *key);
    void (*valDestructor)(void *privdata, void *obj);
} dictType;

typedef struct dictht {
    dictEntry **table;
    unsigned long size;
    unsigned long sizemask;
    unsigned long used;
} dictht;

typedef struct dict {
    dictType *type;
    void *privdata;
    dictht ht[2];
    long rehashidx; 
    unsigned long iterators;
    int persist;
} dict;

typedef struct redisObject {
    unsigned type:4;
    unsigned encoding:4;
    unsigned lru:24; 
    int refcount;
    void *ptr;
} robj;

struct serverRoot {
    int numDb;
    dict ** dicts;
};

/**
 * Field name enumerations
 */

enum PT_dict_fields {
    _field_dict_type,
    _field_dict_privdata,
    _field_dict_ht_table,
    _field_dict_ht_size,
    _field_dict_ht_sizemask,
    _field_dict_ht_used,
    _field_dict_rehashidx,
    _field_dict_iterators,
    _field_dict_persist,
    _field_dict_COUNT
};

extern size_t PT_dict_offset[_field_dict_COUNT];
extern size_t PT_dict_size[_field_dict_COUNT];

enum PT_dictEntry_fields {
    _field_dictEntry_key,
    _field_dictEntry_v,
    _field_dictEntry_next,
    _field_dictEntry_COUNT
};

extern size_t PT_dictEntry_offset[_field_dictEntry_COUNT];
extern size_t PT_dictEntry_size[_field_dictEntry_COUNT];

enum PT_robj_fields {
    _field_robj_type,
    _field_robj_encoding = _field_robj_type,
    _field_robj_lru,
    _field_robj_refcount,
    _field_robj_ptr,
    _field_robj_COUNT
};

extern size_t PT_robj_offset[_field_robj_COUNT];
extern size_t PT_robj_size[_field_robj_COUNT];

enum PT_sdshdr5_fields {
    _field_sdshdr5_b_size,
    _field_sdshdr5_b_type = _field_sdshdr5_b_size,
    _field_sdshdr5_str,
    _field_sdshdr5_COUNT
};

extern size_t PT_sdshdr5_offset[_field_sdshdr5_COUNT];
extern size_t PT_sdshdr5_size[_field_sdshdr5_COUNT];

enum PT_sdshdr8_fields {
    _field_sdshdr8_len,
    _field_sdshdr8_alloc,
    _field_sdshdr8_b_size,
    _field_sdshdr8_b_type = _field_sdshdr8_b_size,
    _field_sdshdr8_str,
    _field_sdshdr8_COUNT
};

extern size_t PT_sdshdr8_offset[_field_sdshdr8_COUNT];
extern size_t PT_sdshdr8_size[_field_sdshdr8_COUNT];

enum PT_sdshdr16_fields {
    _field_sdshdr16_len,
    _field_sdshdr16_alloc,
    _field_sdshdr16_b_size,
    _field_sdshdr16_b_type = _field_sdshdr16_b_size,
    _field_sdshdr16_str,
    _field_sdshdr16_COUNT
};

extern size_t PT_sdshdr16_offset[_field_sdshdr16_COUNT];
extern size_t PT_sdshdr16_size[_field_sdshdr16_COUNT];

enum PT_sdshdr32_fields {
    _field_sdshdr32_len,
    _field_sdshdr32_alloc,
    _field_sdshdr32_b_size,
    _field_sdshdr32_b_type = _field_sdshdr32_b_size,
    _field_sdshdr32_str,
    _field_sdshdr32_COUNT
};

extern size_t PT_sdshdr32_offset[_field_sdshdr32_COUNT];
extern size_t PT_sdshdr32_size[_field_sdshdr32_COUNT];

enum PT_sdshdr64_fields {
    _field_sdshdr64_len,
    _field_sdshdr64_alloc,
    _field_sdshdr64_b_size,
    _field_sdshdr64_b_type = _field_sdshdr64_b_size,
    _field_sdshdr64_str,
    _field_sdshdr64_COUNT
};

extern size_t PT_sdshdr64_offset[_field_sdshdr64_COUNT];
extern size_t PT_sdshdr64_size[_field_sdshdr64_COUNT];

/* Metadata interpretation functions */
int offset_to_field(int type /* PersistType */, size_t offset);
size_t offset_of_field(int type, int field);
size_t size_of_field(int type, int field);
size_t get_size_for_type(int type);

/* Invariant checking functions */
void init_translation_tables();
void generate_diff_record(container * c,
                          int field_id,
                          int index,
                          void* old_addr,
                          void* new_addr,
                          size_t field_size);
void check_deferred_invariants();

