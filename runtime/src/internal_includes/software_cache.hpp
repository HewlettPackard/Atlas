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
 
/**
  *    -- c++ --
  * 
  *    This is a software cache, which helps reduce cache line write backs for consistent persistency.
  *
  *    Brief design:
  *        1. It is an O(1) time implementation. 
  *        2. Data structures: a linked list + a hash table.
  *            2.1. The software cache is organized in a linked list;
  *            2.2. The hash table is used to find the node in the cache linked list, given a cacheline. 
  *        3. Online locality modeling.
  *            3.1  Based on MRC calculated by Li et al.'s ISMM14 work.
  *            3.2  Use bursty sampling.
  *        4. the optimization of the expiration list.
  *            4.1  Not used currently. But retain its code. Need more time to refine it.
  * 
  */

#ifndef _L_CACHE_H_
#define _L_CACHE_H_

#if defined(_USE_SCACHE)

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include "sc_data_structures.hpp" /* doubly linked list, stack and hash table */

/* the optimization of if-statement branches */
#define likely(x)         __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

/**
 * An attribute that will cause a variable or field to be aligned so that
 * it doesn't have false sharing with anything at a smaller memory address.
 */
#define ATTR_ALIGN_TO_AVOID_FALSE_SHARING __attribute__((__aligned__(128)))

/* cache related macros */
#define DEFAULT_CACHE_LINE_SIZE           (64)
#define DEFAULT_CACHE_LINE_MASK         (0xffffffffffffffff - DEFAULT_CACHE_LINE_SIZE + 1)
#define CONFIG_CACHE_SIZE                       (8)     /* cache size by default */
#define CONFIG_INITIAL_CACHELINE         (0x0000000000000040)   /* next cacheline: + 0x40 */

#define DEFAULT_BUCKET_SIZE                   (15)   /*hash table size */

/* locality modeling related macros */
#define LOCALITY_MODELING_SWITCH      (1)
#if (LOCALITY_MODELING_SWITCH)
#define LOCALITY_MODELING
#undef OFFLINE_CACHE
#else
#undef LOCALITY_MODELING
#define OFFLINE_CACHE    /* For OFFLINE_CACHE, a user specifies the best cache size and runs through a program. */
#endif

#if defined(LOCALITY_MODELING)
#define CACHE_RUNTIME_ADJUSTABLE   /* We can only output MRC trace without adjusting cache size. */
#undef MRC_TRACE                                      /* For debug, output MRC trace. */
#endif

#define MAX_NUM_DATA                               (64*1024)     /* maximal number of data we process */
#define MAX_NUM_TRACE                             (1024*1024)  /* maximal trace length we process */
#define MAX_NUM_FASES                             (100)             /* maximal number of FASEs we process */
#define CACHE_HRATIO_THRESHOLD          (0.98)            /* cache hit ratio threshold. */

/**
  * As tested, cache of size greater than 50 incurs significantly long stall at the end of a FASE.
  * We empirically assume window size of 1000 would fill the cache of size 50. 
  */
#define MAX_MODELING_CACHE                  (50)
#define MAX_MODELING_WINDOW              (1000)  /*The maximal window size that we process */

/* bursty sampling to calculate cache size periodically */
#define HIBERNATION_PERIOD                    (1024*1024*1024)
#define BURST_PERIOD                                 MAX_NUM_TRACE

/* UNUSED: the optimization of timeout list */
#undef TIMEOUT_CHECK
#define TIMEOUT_CHECK_PERIOD               (10000)

#undef  CACHE_SYNC_METHOD1
#define CACHE_SYNC_METHOD2

/* for stats, debug, and trace */
#define CACHE_STATS
#undef CACHE_DEBUG
#undef CACHE_TRACE


/* per-thread counter of cache line flushes */
extern __thread int32_t data_flushes;

/* per-thread tag to mark if a cache is initialized */
extern __thread bool is_cache_initialized;

/* the seed of hash key */
uint32_t hash_rd = 0x20150707;

typedef struct _HashTable_t           HashTable_t;
typedef struct _LCacheLine_t          LCacheLine_t;
typedef struct _LCache_t                LCache_t;
typedef struct _LocalityData_t	        LocalityData_t;
typedef struct _Stats_t			Stats_t;

/**
  *  Data structure of hash table element.
  *  Usage: 
  *    1. pointing to a cacheline node in the linked list of the cache;
  *    2. pointing to a datum in locality modeling.
  */
template<class T>
struct HashTableElem_t{
    struct hlist_node                               list;
    intptr_t                                              cacheline;			
    T                                                       list_node;			/* T: LCacheLine_t * or int32_t */
} ATTR_ALIGN_TO_AVOID_FALSE_SHARING;

/**
  *  Data structure of hash table.
  *  Usage: 
  *    1. to find a cacheline in O(1) time.
  */
struct _HashTable_t { 
    struct hlist_head                               *bucket;
    uint64_t                                            bucket_size;
    uint64_t                                            bucket_mask;
} ATTR_ALIGN_TO_AVOID_FALSE_SHARING;

/**
  *  Data structure of a cache line.
  *  Usage: 
  *    1. a node in the linked list of the software cache.
  */
struct _LCacheLine_t {
    struct list_head                                 list;
    intptr_t                                              cacheline;			 /* cache line address i.e. addr >> 6. */
    int64_t                                              expire_time;		 /* when the cacheline can be ejected. */
    HashTableElem_t<LCacheLine_t *>    *hash_node;               /* for an easy traversal. */
    HashTableElem_t<int32_t>                *locality_hash_node; /* for an easy traversal. */
    //    struct list_head        expire_list;                                      /* not designed yet */
} ATTR_ALIGN_TO_AVOID_FALSE_SHARING;

/**
  *  Data structure of locality modeling.
  *  Usage: 
  *    1. contains all data regarding locality.
  */
struct _LocalityData_t {
    int32_t                                              n;                                    /* trace length */
    int32_t                                              m;                                   /* the number of distinct data */
    int32_t                                              *F;                                  /* first access times of data */
    int32_t                                              *L;                                  /* last access times of data */
    int32_t                                              *reuse_time;                 /* reuse time histogram */
    HashTable_t                                       *hash_table;                 /* to O(1) find the cache line data */
    struct list_head                                 all_distinct_cachelines;  /* a linked list to organize all data */
    int32_t                                              split_times;                    /* number of FASEs that we process */
} ATTR_ALIGN_TO_AVOID_FALSE_SHARING;

/**
  *  Data structure of data statistics.
  */
struct _Stats_t {
    int32_t                                              async_calls;		/* number of calls to AsyncDataFlush */
    int32_t                                              async_memop_calls;	/* number of calls to AsyncMemOpDataFlus */
    int32_t						     sync_calls;			/* number of calls to SyncDataFlush */
    int32_t						     actual_sync_calls;	/* number of calls to SyncDataFlush for non-zero flush*/
    int32_t						     sync_data_flushes;	/* number of data flushes in SyncDataFlush */
} ATTR_ALIGN_TO_AVOID_FALSE_SHARING;

/**
  *  Data structure of the software cache.
  */
struct _LCache_t {
    size_t                                                size;				/* adjustable cache size */
    size_t                                                actual_size;		/* how many cache lines are occupied */
    struct list_head                                 the_cache;			/* software cache, full asso, LRU */
    HashTable_t                                      *hash_table;		/* attached hash table for the Cache */
    LocalityData_t                                   *locality_data;		/* locality modeling */
    int32_t                                              hibernate_period;	/* hibernation time long */
    int32_t                                              burst_period;		/* burst time long */
    int32_t                                              sampling_state;		/* 0- burst state, 1- hibernate state */
    int32_t                                              sampling_counter;	/* sampling countdown */
    int32_t                                              expire_check_period;/* how often to expire check */
    int64_t                                              fill_time;			/* cache fill time */
    int64_t                                              accesses;			/* total accesses */
    int64_t						     misses;		        /* total misses */
    int64_t						     hits;				/* total hits */
    Stats_t						     stats;				/* statistics of the cache */
    pthread_t					     tid;				/* pthread id */
    bool						     modeling_switch;	/* true - on or false - off */
    //    HashTable_t              *timeout_hash_table;                   /* not designed yet. */
} ATTR_ALIGN_TO_AVOID_FALSE_SHARING;

/* ---- hash table implementation ---- */

/* calculate hash key, given a cacheline */
static inline uint64_t hashkey( HashTable_t * ht , intptr_t cacheline )
{
    return ((uint64_t) cacheline) & ht->bucket_mask ;
}

/* add an element to a hash table */
template<class T>
inline void  HashTableAdd( HashTable_t * ht, HashTableElem_t<T> * elem )
{
    uint64_t hash = hashkey(ht, elem->cacheline);
    hlist_add_head( &elem->list, &ht->bucket[hash] );
}

/* get an element in a hash table, given a cacheline */
template<class T>
static inline HashTableElem_t<T> * HashTableGetElem( HashTable_t * ht, intptr_t cacheline )
{
    uint64_t hash = hashkey(ht, cacheline);
    HashTableElem_t<T> * elem;
    struct hlist_node *n;

   hlist_for_each_entry_cxx11(elem, n, &ht->bucket[hash], list) {
        if ( likely(cacheline == elem->cacheline) ) {
            return elem;
        }
   }
	
    return NULL;
}

/* delete an element from a hash table, given a cacheline */
template<class T>
inline HashTableElem_t<T> * HashTableDel(HashTable_t * ht, intptr_t cacheline )
{
    HashTableElem_t<T> * elem = HashTableGetElem<T>(ht, cacheline);
    hlist_del(&elem->list);

    return elem;
}

/** 
 * update a cacheline entry with new cache line address. need to do :
 * 1. change the cache line address of the entry;
 * 2. get rid of the "key-value" pair from hash table due to change of key;
 * 3. add new hash element to the hash table.
 */
template<class T>
inline void HashTableUpdate(HashTable_t * ht, intptr_t old_cacheline, intptr_t new_cacheline )
{
    /* key changes, thus chang to another bucket. */
    HashTableElem_t<T> * elem = HashTableDel<T>( ht, old_cacheline );

    /* update the new pointed cache line */
    elem->cacheline = new_cacheline;

    /* NOTE: no need to change the pointed link node, because the linke node's address keeps the same*/
    // elem->list_node = new_node;
	
    HashTableAdd<T>(ht, elem);
}

/* get the node in the linked list of the software cache, given a cacheline */
template<class T>
inline T HashTableGet( HashTable_t * ht, intptr_t cacheline )
{
    uint64_t hash = hashkey(ht, cacheline);
    HashTableElem_t<T> * elem;
    struct hlist_node *n;

    /* a quick path */
    n = ht->bucket[hash].first;
    if ( unlikely(!n) ) return 0;
    elem = hlist_entry_cxx11( n, HashTableElem_t<T>, list);
    if ( likely(cacheline == elem->cacheline) ) return elem->list_node;

    hlist_for_each_entry_cxx11(elem, n, &ht->bucket[hash], list) {
        if ( likely(cacheline == elem->cacheline) ) {
            return elem->list_node;
        }
    }

    return 0; /* 0 works for both LCacheLine_t * and int32_t */
}

/* ---- double linked list implementation ---- */

/* get the head of a linked list */
static inline LCacheLine_t * DLLHead( struct list_head * head )
{
    LCacheLine_t * entry;
    entry = list_first_entry_cxx11(head, LCacheLine_t, list);

    return entry;
}

/* get the tail of a linked list */
static inline LCacheLine_t * DLLTail( struct list_head * head )
{
    LCacheLine_t * entry;
    entry = list_entry_cxx11(head->prev, LCacheLine_t, list);

    return entry;
}

/* move the node "list" to the head of the linked list "head" */
static inline void DLLUpdateHead( struct list_head * head, struct list_head * list )
{
    /* list_del(list); */
    list->next->prev = list->prev;
    list->prev->next = list->next;

    /* list_add(list, head); */
    struct list_head *next;
    next = head->next;
    next->prev = list;
    list->next = next;
    list->prev = head;
    head->next = list;
}

/* move the node "list" to the tail of the linked list "head" */
static inline void DLLMoveToTail( struct list_head * head, struct list_head * list )
{
    list_move_tail(list, head);
}

/* return the "next" cacheline in the linked list of the cache */
intptr_t LCacheNextAvaiableCacheline( LCache_t * theCache, size_t next )
{
    assert( next < theCache->size );
    
    LCacheLine_t * tmp;
    LCacheLine_t * n;
    size_t i =0;
    list_for_each_entry_safe_cxx11( tmp, n, &theCache->the_cache, list ) {
        if ( i++ == next ) return tmp->cacheline;
    }

    return 0;
}

/* Update the cacheline info in the cache, given an address */
static inline intptr_t LCacheUpdate( LCache_t * theCache, intptr_t new_address )
{
    intptr_t new_cacheline = new_address & DEFAULT_CACHE_LINE_MASK ;
    LCacheLine_t * entry = HashTableGet<LCacheLine_t *>( theCache->hash_table, new_cacheline ) ;

    if ( likely( entry != 0 ) ) {
#if defined(CACHE_STATS)
        theCache->hits ++;
#endif
        /* LRU algorithm: move the found node to the head */
        DLLUpdateHead( &theCache->the_cache, &entry->list );
        //entry->expire_time = theCache->accesses;

#if defined(CACHE_TRACE)
	fprintf(stderr, "LCacheUpdate, hit entry: %p, new: %p, data flushes: %d, misses: %d\n", 
	        entry->cacheline, new_cacheline, data_flushes, theCache->misses);
#endif
        return entry->cacheline;
    }
    else {
#if defined(CACHE_STATS)
	theCache->misses ++;
#endif
	LCacheLine_t * tail_entry = DLLTail ( &theCache->the_cache );
	assert (tail_entry);

    	/* 1. move the tail node to the head. LRU algorithm. */
	DLLUpdateHead( &theCache->the_cache, &tail_entry->list);

	/* 2. record the new cacheline */	
	intptr_t old_cacheline	= tail_entry->cacheline;
	tail_entry->cacheline   = new_cacheline;
        // tail_entry->expire_time = theCache->accesses;

	/* 3. update the hash table */
	HashTableElem_t<LCacheLine_t *> * elem = tail_entry->hash_node;
	hlist_del(&elem->list);
	elem->cacheline = new_cacheline;
	HashTableAdd<LCacheLine_t *>(theCache->hash_table, elem);

#if defined(CACHE_TRACE)
	fprintf(stderr, "LCacheUpdate, old : %p, new: %p, data flushes: %d, misses: %d\n", 
	        old_cacheline, new_cacheline, data_flushes, theCache->misses);
#endif

        return old_cacheline;
    }
}

static inline bool IsCacheFull(LCache_t * theCache )
{
    return theCache->size == theCache->actual_size;
}

void Modeling( LCache_t * theCache, intptr_t new_address );
void ExpireCheck( LCache_t * theCache );

/* the main entrance of the cache */
inline intptr_t LCacheOpEntrance( LCache_t * theCache, intptr_t new_address )
{
#if defined(CACHE_STATS)
    theCache->accesses ++;
#endif

    /* 1. locality modeling */
#if defined(LOCALITY_MODELING)
    if ( unlikely(theCache->modeling_switch == true) ) {
	Modeling( theCache, new_address );
    }
#endif

    /* 2. update software cache */
    intptr_t old_cacheline;
    old_cacheline = LCacheUpdate( theCache, new_address );	

    /* 3. expiration check */
#if defined(TIMEOUT_CHECK)
    ExpireCheck( theCache );
#endif

    return old_cacheline;
}

void InitLocalityData( LocalityData_t * locality_data );

/* when a thread starts, its software cache is created. */
LCache_t * LCacheCreation( LCache_t * theCache )
{
    /* init with the default size */
    theCache->size = CONFIG_CACHE_SIZE;
#if defined(OFFLINE_CACHE)
    const char * env = getenv("ATLAS_CACHE_SIZE");
    if ( env != 0  && atoi(env) != 0 ) {
	theCache->size = atoi(env); 
    }
#endif
    fprintf(stderr, "Software Cache Size: %d\n", theCache->size);
    theCache->actual_size = 0;

    /* create the software cache */
    INIT_LIST_HEAD(&theCache->the_cache);

    /* create meta-data: the hash table */
    theCache->hash_table = (HashTable_t *)malloc(sizeof(HashTable_t));
    theCache->hash_table->bucket_size = 1 << DEFAULT_BUCKET_SIZE;
    theCache->hash_table->bucket_mask = theCache->hash_table->bucket_size -1;
    theCache->hash_table->bucket = (struct hlist_head *) malloc 
        (theCache->hash_table->bucket_size * sizeof(struct hlist_head));
    for ( size_t i = 0; i < theCache->hash_table->bucket_size; i++ ) 
        INIT_HLIST_HEAD(&theCache->hash_table->bucket[i]);	 

    /* create the cacheline structure */
    for ( size_t i = 0; i < theCache->size; i++ ) {
        LCacheLine_t * new_cacheline = (LCacheLine_t *) malloc ( sizeof(LCacheLine_t) );
        new_cacheline->cacheline	 = 0; 
        new_cacheline->expire_time	 = -1;
        list_add_tail(&new_cacheline->list, &theCache->the_cache);

        HashTableElem_t<LCacheLine_t *> * new_hash_node = 
              (HashTableElem_t<LCacheLine_t *> *) malloc ( sizeof(HashTableElem_t<LCacheLine_t *>) );
        new_hash_node->cacheline = 0; 
        new_hash_node->list_node = new_cacheline;
        INIT_HLIST_NODE(&new_hash_node->list);
        HashTableAdd<LCacheLine_t *>( theCache->hash_table, new_hash_node);

        new_cacheline->hash_node = new_hash_node;
    }

    /* create thread-local locality related data */
    theCache->locality_data = (LocalityData_t *)malloc(sizeof(LocalityData_t));
	assert ( theCache->locality_data != 0 );
    InitLocalityData(theCache->locality_data);

    /* initialize for bursty sampling */
    theCache->hibernate_period = HIBERNATION_PERIOD;
    theCache->burst_period = BURST_PERIOD;
    theCache->sampling_state = 0; /* a burst state to start with */
    theCache->sampling_counter = theCache->burst_period ;

    /* initialize related data structures with expiration check */
    theCache->fill_time = 100;
    char * env2= getenv("ATLAS_CACHE_FILL_TIME");
    if ( env2 != 0 ) {
	theCache->fill_time = atoi(env2); 
    }
    //fprintf(stderr, "Software Cache Fill Time: %d\n", theCache->fill_time);
    theCache->accesses = 0;
    theCache->expire_check_period = TIMEOUT_CHECK_PERIOD;

    /* initialize stats */
    theCache->hits = 0;
    theCache->misses = 0;
    theCache->stats.async_calls = 0;
    theCache->stats.async_memop_calls = 0;
    theCache->stats.sync_calls = 0;
    theCache->stats.actual_sync_calls = 0;
    theCache->stats.sync_data_flushes = 0;

    /* get thread id */
    theCache->tid = pthread_self();

    /* locality modeling on by default */
    theCache->modeling_switch = true;

    return theCache;
}

/* print the current cache contents */
void PrintCache( LCache_t * theCache )
{
    LCacheLine_t * tmp;
    LCacheLine_t * n;
    size_t i =0;
    list_for_each_entry_safe_cxx11( tmp, n, &theCache->the_cache, list ) {
	fprintf(stderr, "cacheline: %p, expire: %d\n", tmp->cacheline, tmp->expire_time);
    }

}

void ClearHashTableForSplitFases( LocalityData_t* );

/* clear cache at the end of a FASE */
void ResetCache( LCache_t * theCache ) 
{
    LCacheLine_t * tmp;
    LCacheLine_t * n;
    size_t i =0;
    list_for_each_entry_safe_cxx11( tmp, n, &theCache->the_cache, list ) {
	tmp->cacheline		= 0; 
	tmp->expire_time	= -1;
	HashTableElem_t<LCacheLine_t *> * elem = tmp->hash_node;
	hlist_del(&elem->list);
	elem->cacheline = 0;
	HashTableAdd<LCacheLine_t *>(theCache->hash_table, elem);	
    }

#if defined(LOCALITY_MODELING)
    /* At the end of a FASE, we empty the locality data. */
    ClearHashTableForSplitFases( theCache->locality_data);
#endif
}

void DestroyLocalityData( LocalityData_t * locality_data, pthread_t tid );

/* destroy the software cache */
void LCacheDestroy( LCache_t * theCache )
{
    fprintf(stderr,"calling LCacheDestroy! \n");
    LCacheLine_t * tmp;
    LCacheLine_t * n;
    HashTableElem_t<LCacheLine_t *> * m;
    list_for_each_entry_safe_cxx11(tmp, n, &theCache->the_cache, list) {
        m = tmp->hash_node;
	hlist_del(&m->list);
        free(m);
        list_del(&tmp->list);
        free(tmp);
    }

    if (theCache->hash_table) { 
	if (theCache->hash_table->bucket) 
	    free(theCache->hash_table->bucket);
	free(theCache->hash_table);
    }

    theCache->actual_size = 0;
    theCache->size = 0;
    theCache->hash_table = NULL;

    /* destroy the locality data attached to the cache */
    DestroyLocalityData( theCache->locality_data, theCache->tid );
    free (theCache->locality_data);
    theCache->locality_data = 0;

    theCache->modeling_switch = false;

    is_cache_initialized = false;
}

void LCacheSyncDataFlush( LCache_t * theCache );

/* adjust cache size at run-time online. cache size may increase or decrease. */
void LCacheExpansion( LCache_t * theCache, size_t new_size )
{
    /* expand cache */
    if ( new_size > theCache->size ) {
        size_t increased_size = new_size - theCache->size;
        
        for ( size_t i = 0; i < increased_size; i++ ) {
            LCacheLine_t * new_cacheline = (LCacheLine_t *) malloc ( sizeof(LCacheLine_t) );
            new_cacheline->cacheline     = 0; 
            new_cacheline->expire_time   = -1;
            list_add_tail(&new_cacheline->list, &theCache->the_cache);

            HashTableElem_t<LCacheLine_t *> * new_hash_node 
                    = (HashTableElem_t<LCacheLine_t *> *) malloc ( sizeof(HashTableElem_t<LCacheLine_t *>) );
            new_hash_node->cacheline = 0;
            new_hash_node->list_node = new_cacheline;
            INIT_HLIST_NODE(&new_hash_node->list);
            HashTableAdd<LCacheLine_t *>( theCache->hash_table, new_hash_node);

	    new_cacheline->hash_node = new_hash_node;
        }
    }
    
    /* TODO: can be optimized, only flush the decreased cache entries. */
    /* shrink cache */
    if ( new_size < theCache->size ) {
        /* flush all the cachelines before shrinking */
        LCacheSyncDataFlush( theCache );

        size_t decreased_size = theCache->size - new_size;
        size_t i =0;
        LCacheLine_t * tmp;
        LCacheLine_t * n;
        HashTableElem_t<LCacheLine_t *> * m;        
        list_for_each_entry_safe_cxx11( tmp, n, &theCache->the_cache, list) {
            m = tmp->hash_node;
	        hlist_del(&m->list);
            free(m);
            list_del(&tmp->list);
            free(tmp);

            if ( ++i == decreased_size ) break;
        }
    }     

    /* update the new cache size */
    theCache->size = new_size;
    assert( theCache->actual_size == 0 );
}

/* print out stats */
void PrintStats( LCache_t * theCache ) 
{
    fprintf(stderr, "tid: %p, async flush:    %10d\n", theCache->tid, theCache->stats.async_calls);
    fprintf(stderr, "tid: %p, asyn mem flush: %10d\n", theCache->tid, theCache->stats.async_memop_calls);
    fprintf(stderr, "tid: %p, sync calls:     %10d\n", theCache->tid, theCache->stats.sync_calls);
    fprintf(stderr, "tid: %p, real sync call: %10d\n", theCache->tid, theCache->stats.actual_sync_calls);
    fprintf(stderr, "tid: %p, syn data flush: %10d\n", theCache->tid, theCache->stats.sync_data_flushes);
    float flushes_per_syn_call = 0.0;
    if ( theCache->stats.actual_sync_calls ) {
	flushes_per_syn_call = ((float)theCache->stats.sync_data_flushes)/theCache->stats.actual_sync_calls;
    }
    fprintf(stderr, "tid: %p, d-flush/syncall:%10.5f\n", theCache->tid, flushes_per_syn_call);
    fprintf(stderr, "tid: %p, d-flush/syncall(over cache size):%10.5f\n", theCache->tid, flushes_per_syn_call/theCache->size);

}

/* export interface to report cache misses */
void LCacheStats( LCache_t * theCache )
{
    fprintf(stderr, "tid: %p, cache accesses: %10d\n", theCache->tid, theCache->accesses);
    fprintf(stderr, "tid: %p, cache hits:     %10d\n", theCache->tid, theCache->hits);
    fprintf(stderr, "tid: %p, cache misses:   %10d\n", theCache->tid, theCache->misses);
    float mr;
    if ( theCache->accesses != 0 ) {
	mr = ((float)theCache->misses) / theCache->accesses;
    }
    else {
	mr = 0.0;
    }
    fprintf(stderr, "tid: %p, cache m-ratio:  %10.5f\n", theCache->tid, mr);
    fprintf(stderr, "tid: %p, cache       n:  %10d\n", theCache->tid, theCache->locality_data->n);
    fprintf(stderr, "tid: %p, cache       m:  %10d\n", theCache->tid, theCache->locality_data->m);

    PrintStats( theCache );
}

/* --------------  Cache model  -------------- */

/* reset the locality data to zero for the next FASE */
static void ResetZero( LocalityData_t * locality_data )
{
    locality_data->n = 0;
    locality_data->m = 0;
    memset(locality_data->F, 0, sizeof(int32_t) * MAX_NUM_DATA);
    memset(locality_data->L, 0, sizeof(int32_t) * MAX_NUM_DATA);
    memset(locality_data->reuse_time, 0, sizeof(int32_t) * MAX_NUM_TRACE);

    for ( size_t i = 0; i < locality_data->hash_table->bucket_size; i++ ) {
        HashTableElem_t<int32_t> * elem;
        struct hlist_node *n;
	    struct hlist_node *tmp;

        hlist_for_each_entry_safe_cxx11(elem, n, tmp, &locality_data->hash_table->bucket[i], list) {
            hlist_del(&elem->list);
            free(elem);
        }
    }
    locality_data->split_times = 0;
}

/* initialize locality data */
void InitLocalityData( LocalityData_t * locality_data )
{
    locality_data->n = 0;
    locality_data->m = 0;
    locality_data->F = (int32_t *)malloc(sizeof(int32_t) * MAX_NUM_DATA);
    locality_data->L = (int32_t *)malloc(sizeof(int32_t) * MAX_NUM_DATA);
    locality_data->reuse_time = (int32_t *)malloc(sizeof(int32_t) * MAX_NUM_TRACE);
    locality_data->split_times = 0;

    /* create the linked list which stores all distinct cachelines */
    INIT_LIST_HEAD(&locality_data->all_distinct_cachelines);

    /* create the hash table to O(1) find the cache line in the linked list */
    locality_data->hash_table = (HashTable_t *)malloc(sizeof(HashTable_t));
    locality_data->hash_table->bucket_size = 1 << DEFAULT_BUCKET_SIZE;
    locality_data->hash_table->bucket_mask = locality_data->hash_table->bucket_size -1;
    locality_data->hash_table->bucket = (struct hlist_head *) malloc 
        (locality_data->hash_table->bucket_size * sizeof(struct hlist_head));
    for ( size_t i = 0; i < locality_data->hash_table->bucket_size; i++ ) {
        INIT_HLIST_HEAD(&locality_data->hash_table->bucket[i]);	 
    }
    
    ResetZero(locality_data);
}

void OutputMRCTrace( LocalityData_t * locality_data, pthread_t tid );

/* destroy locality data */
void DestroyLocalityData( LocalityData_t * locality_data, pthread_t tid )
{
#if defined(MRC_TRACE)
    fprintf(stderr, "OutputMRCTrace at destoryLocalityData moment.\n");
    OutputMRCTrace(locality_data, tid);
#endif

    locality_data->n = 0;
    locality_data->m = 0;
    free(locality_data->F);
    locality_data->F = 0;
    free(locality_data->L);
    locality_data->L = 0;
    free(locality_data->reuse_time);
    locality_data->reuse_time = 0;
    locality_data->split_times = 0;

    LCacheLine_t * tmp;
    LCacheLine_t * n;
    size_t i =0;
    list_for_each_entry_safe_cxx11( tmp, n, &locality_data->all_distinct_cachelines, list ) {
        HashTableElem_t<int32_t> * elem = tmp->locality_hash_node;
	hlist_del(&elem->list);
        free(elem);
        list_del(&tmp->list);
        free(tmp);
    }

    if (locality_data->hash_table) {
	if (locality_data->hash_table->bucket) { 
	    free(locality_data->hash_table->bucket);
	    locality_data->hash_table->bucket = 0;
	}
	free(locality_data->hash_table);
	locality_data->hash_table = 0;
    }
}

/* we need reset locality data at the end of a FASE */
void ClearHashTableForSplitFases( LocalityData_t * locality_data )
{
    /* clear all the cache-line data and the hash table for the locality data. */
    LCacheLine_t * tmp;
    LCacheLine_t * n;
    size_t i =0;
    list_for_each_entry_safe_cxx11( tmp, n, &locality_data->all_distinct_cachelines, list ) {
        HashTableElem_t<int32_t> * elem = tmp->locality_hash_node;
	hlist_del(&elem->list);
        free(elem);
        list_del(&tmp->list);
        free(tmp);
    }

    /* update the counter of FASEs */
    locality_data->split_times ++;
}

/**  
  * Trace processing:
  *     Given a cacheline, update its first access time, last access time and reuse time.
  *     They are the meta-data used to compute MRC.
  */
void DataAccess( LocalityData_t * locality_data, intptr_t cacheline )
{
    /* we skip data accesses beyond maximumal trace length */
    /* the maximum of n is MAX_NUM_TRACE */
    if ( locality_data->n == MAX_NUM_TRACE ) return;

    HashTableElem_t<int32_t> * elem = 
		HashTableGetElem<int32_t>( locality_data->hash_table, cacheline );

    /* not found */
    if ( elem == 0 ) {
        /* create hash table element */
        int32_t index = locality_data->m;
        
        /* we skip more data than MAX_NUM_DATA */
        if ( index == MAX_NUM_DATA ) return; 

	/* increase time counter */
	++ locality_data->n;
	int32_t time = locality_data->n;

        /* update first access time and last access time */
        locality_data->F[index] = time;
        locality_data->L[index] = time;

        /**
          * We should create a list to store all the distinct data we have accessed
          * between FASEs. We cannot use the software cache data structure, since
          * the software cache doesn't necessarily have all the distinct data we have 
          * accessed, because of cache size.
          */  
        LCacheLine_t * new_cacheline  = (LCacheLine_t *) malloc ( sizeof(LCacheLine_t) );
        new_cacheline->cacheline	          = cacheline;
        list_add_tail(&new_cacheline->list, &locality_data->all_distinct_cachelines);
        
        HashTableElem_t<int32_t> * new_hash_node = 
            (HashTableElem_t<int32_t> *) malloc ( sizeof(HashTableElem_t<int32_t>) );
        new_hash_node->cacheline = cacheline;
	new_hash_node->list_node = index;
	INIT_HLIST_NODE(&new_hash_node->list);
	HashTableAdd<int32_t>( locality_data->hash_table, new_hash_node);

        new_cacheline->locality_hash_node = new_hash_node;

        /* the number of data increments */
        ++ locality_data->m; 
    }
    else {
	/* increase time counter */
	++ locality_data->n;
	int32_t time = locality_data->n;
    
        int32_t index = elem->list_node;
        assert( index >= 0 );
        assert( index < MAX_NUM_DATA );

        /* update reuse time and last access time */
        int rt = time - locality_data->L[index];
        assert( rt < MAX_NUM_TRACE );
	assert( rt < locality_data->n );
        locality_data->L[index] = time;
        locality_data->reuse_time[rt] ++;
    }
}

/* calculate meta-data for MRC, the meta-data is stored in tmpArr. */
void PrepLiveness( LocalityData_t * locality_data, double *tmpArr )
{
    int32_t m = locality_data->m;
    int32_t n = locality_data->n;
    int32_t * numberOfWindows = (int32_t *)malloc(sizeof(int32_t)*(n+1));
    memset (numberOfWindows, 0, sizeof(int32_t) * (n+1));

    /* 1. compute histograms of first access times and last access times */
    for ( int32_t i=0; i<m; i++ ) {
        numberOfWindows[ locality_data->F[i] ] ++;

        int32_t L_tmp = n + 1 - locality_data->L[i];
        numberOfWindows[ L_tmp ] ++;
    }

    /* 2. compute cumulative histograms */
    for ( int32_t i=n-1; i>=1; i-- ) {
        numberOfWindows[i] += numberOfWindows[i+1];

        tmpArr[i] = numberOfWindows[i+1] + tmpArr[i+1];
    }
    free(numberOfWindows);

    int64_t a=0;
    int64_t b=0;
    /* 3. store meta-data into tmpArr */
    for ( int32_t i=n-1; i>=1; i-- ) {
        tmpArr[i] += a - b * i;
        a += i * locality_data->reuse_time[i];
        b += locality_data->reuse_time[i];
    }
}

/* calculate liveness given a window size. */
double Liveness( LocalityData_t * locality_data, int32_t window, double *tmpArr )
{
    double tmp;

    tmp = tmpArr[window];
    double liveness = locality_data->m - tmp / ( locality_data->n - window + 1 );
                
    return liveness;
}

/**
  * NOT USED !!! But it is useful to retain.
  * It clearly shows how to compute liveness of a specific window size.
  */
double Liveness2( LocalityData_t * locality_data, int32_t window )
{
    int64_t tmp = 0;

    /* the number of distinct data */
    int32_t m = locality_data->m;

    /* trace length */
    int32_t n = locality_data->n;

    /* window size */
    int32_t w = window;

    for ( int32_t i=0; i<m; i ++ ) {
        if ( locality_data->F[i] > w ) {
            tmp += locality_data->F[i] - w;
        }
        int32_t L_tmp = n + 1 - locality_data->L[i];
        if ( L_tmp > w ) {
            tmp += L_tmp - w;
        }
    }

    for ( int32_t i=w+1; i <= n - 1; i ++ ) {
        tmp += (i - w) * locality_data->reuse_time[i];
    }
	int32_t t=0;
	for ( int32_t i=1; i <= n - 1; i ++ ) {
		t += locality_data->reuse_time[i];
	}

    double liveness = m - ((double)tmp) / (n-w+1);

    return liveness;
}

/* output MRC curve */
void OutputMRCTrace( LocalityData_t * locality_data, pthread_t tid )
{
    static int32_t trace_i = 0;
    char trace_file[1024] = {'\0'};
    sprintf(trace_file, "tid%p_mrctrace%d.out", tid, trace_i++);

    FILE * fd = fopen(trace_file, "w");

    int32_t n = locality_data->n;
    int32_t m = locality_data->m;

    double * tmpArr = (double *)malloc(sizeof(double) * (n+1));
    memset( tmpArr, 0, sizeof(double) * (n+1));

    /* get the meta-data to calculate liveness */
    PrepLiveness( locality_data, tmpArr );

    /* compute liveness of window size of 1 */
    double live2 = Liveness( locality_data, 1, tmpArr );
    double screw = live2 - 1;
    fprintf(fd,"n=%5d m=%5d split_times=%5d screw_parameter=%15.5f\n",n ,m, locality_data->split_times, screw);
    fprintf(fd, "%15s%15s%15s\n", "fill-time", "cache-size", "miss-ratio");
    fprintf(fd, "%15d%15.5f%15.5f\n", 0, 0.0, 1.0);
    int32_t hits = 0;
    for ( int32_t i=1; i<=n-1; i++ )  {
        hits += locality_data->reuse_time[i];
        double live = Liveness( locality_data, i, tmpArr );
        float mr =  ((float)(n-hits)) / n ;
        fprintf(fd, "%15d%15.5f%15.5f\n", i, live - screw, mr);

        if ( live - screw > MAX_MODELING_CACHE ) break;
	if ( i > MAX_MODELING_WINDOW ) break;
    }

    free(tmpArr);
    fclose(fd);
}

/**
  * Based on MRC, select the best cache capacity. 
  * We use some heuristics to choose cache size from MRC.
  * 1. we choose the top 10 cache sizes in MRC where miss ratio maximally drops;
  * 2. we choose the biggest cache size from the top 10.
  */
int32_t SelectCacheCapacity ( LocalityData_t * locality_data, double * meta_data )
{
    /* As tested, cache size greater than 50 incurs too long stall at the end of a FASE.*/
    int cache_cap_upbound = 50; 

    /* Best cache size */
    int best_cache_cap = 50; 

    /* If a trace longer than 1000 cannot fill cache of 50, we argue that miss ratio of cache of 50 is very little. */
    int fill_time_cap = 1000;  

    /* MRC of cache size from 0 to 50 */
    double miss_ratios[51] = {1.0};

    /* trace length */
    int n = locality_data->n;

    /* the total amount of distinct data */
    int m = locality_data->m;

    /* MRC calculation */
    int curr_cache_sz = 1, fill_time;
    for ( fill_time=1; fill_time < fill_time_cap; fill_time++ ) {
        double liveness = m - meta_data[fill_time] / ( n - fill_time + 1 );

        /* start to calculate miss ratio */
        if ( liveness >= curr_cache_sz ) {
            double next_liveness = m - meta_data[fill_time+1] / ( n - fill_time );
            double mr = next_liveness - liveness;

            /* Theoreotically, it is correct. In practical, need enforcement. */ 
            if ( mr > 1.0 ) mr = 1.0;
            miss_ratios[curr_cache_sz++] = mr;

            if ( curr_cache_sz > cache_cap_upbound ) break;
        }
    }

    /* output the maximal cache size that can be filled by access stream up to size of 1000 . */
    int max_effective_cache_sz = curr_cache_sz - 1;
#if defined(CACHE_DEBUG)
    fprintf(stderr, "maximum effective cache size filled by up to length-1000 trace: %d\n", max_effective_cache_sz);
    fprintf(stderr, "MRC stats:\n");
    for ( int i = 1; i <= max_effective_cache_sz; i++ ) {
        fprintf( stderr, "\t cache %d \t\t mr %f\n", i, miss_ratios[i] );
    }
#endif

    /* calculate gradient of MRC */
    double mr_gradients[51] = {0.0};
    for (int i=1; i <= max_effective_cache_sz ; i++ ) {
        mr_gradients[i] = miss_ratios[i-1] - miss_ratios[i];
        if ( mr_gradients[i] < 0 ) mr_gradients[i] = 0;
    }
#if defined(CACHE_DEBUG)
    fprintf(stderr, "MRC gradients:\n");
    for ( int i = 1; i <= max_effective_cache_sz; i++ ) {
        fprintf( stderr, "\t cache %d \t\t mr %f\n", i, mr_gradients[i] );
    }
#endif

    /* choose top 10 */
    int cache_sizes[11];
    int i = 0, j;
    while ( i < 10 ) {
        double max_gradient = 0.0;
        int max_idx = -1;
        for ( j=1; j <= max_effective_cache_sz; j++ ) {
            if ( mr_gradients[j] > max_gradient ) {
                max_gradient = mr_gradients[j];
                max_idx = j;
            }
        }
        
        if ( max_idx == -1) break;
        cache_sizes[i] = max_idx;
        mr_gradients[max_idx] = 0.0;
        i ++;
    }
#if defined(CACHE_DEBUG)
    fprintf(stderr, "top 10:\n");
    for ( int i = 0; i < 10; i ++ ) {
        fprintf(stderr, "%d\n", cache_sizes[i]);
    }
#endif

    /* choose the biggest cache size */
    int max_v = 0;
    int k =0;
    while ( k < i ) {
        if ( max_v < cache_sizes[k] ) {
            max_v = cache_sizes[k];
        }
        k++;
    }
    assert ( max_v != 0 );

    best_cache_cap = max_v;
    return best_cache_cap;
}

/* adjust the best cache size at run-time */
void AdjustCacheCapacity( LCache_t * theCache )
{
    int32_t n = theCache->locality_data->n;
    double * tmpArr = (double *)malloc(sizeof(double) * (n+1));
    memset( tmpArr, 0, sizeof(double) * (n+1));

    /* compute meta-data for computing MRC */
    PrepLiveness( theCache->locality_data, tmpArr );
    
    /* TODO: we can return fill time in the future, which would be useful. */
    int32_t new_cache_size = SelectCacheCapacity( theCache->locality_data, tmpArr );
    
    free(tmpArr);

    fprintf(stderr, "!!!Adjust cache capacity from %d to %d\n", theCache->size, new_cache_size);
    LCacheExpansion( theCache, new_cache_size );
    //ResetZero( theCache->locality_data );

}

/* the entrance function of locality modeling */
void Modeling( LCache_t * theCache, intptr_t new_address )
{
    intptr_t cacheline = new_address & DEFAULT_CACHE_LINE_MASK;

    /* process the input cache line  */
    DataAccess( theCache->locality_data, cacheline );
    
    /* swith sampling state from burst to hibernate */
    if ( unlikely(-- theCache->sampling_counter == 0) ) {
	theCache->modeling_switch = false;

       /* we could choose either to adjust cache size or to output MRC */
#if defined(CACHE_RUNTIME_ADJUSTABLE)
        /* run-time adaptively adjust cache size */
        AdjustCacheCapacity(theCache);
#endif

#if defined(MRC_TRACE)
        fprintf(stderr, "OutputMRCTrace in the process of tracing.\n");
        OutputMRCTrace( theCache->locality_data, theCache->tid );
#endif
    }
}

/**
  * UNUSED: the optimization of the Expiration List.
  * It would be useful to retain. We can refine the design to let it be useful in the future.
  */
inline void ExpireCheck( LCache_t * theCache )
{
    if ( -- theCache->expire_check_period != 0 ) return;
    theCache->expire_check_period = TIMEOUT_CHECK_PERIOD;

    if ( !IsCacheFull( theCache ) ) return;
    
    struct list_head * node = &theCache->the_cache;
    LCacheLine_t * first_entry = DLLHead(&theCache->the_cache);
    LCacheLine_t * entry;
    do {
        entry = list_entry_cxx11(node->prev, LCacheLine_t, list);

        /* need many heuristics to refine it */
        if ( entry->expire_time + theCache->fill_time <= theCache->accesses ) {
            /* clflush cache lines */
            //AO_nop_full();
            NVM_CLFLUSH(entry->cacheline);
        }
        else{
            break;
        }

        node = node->prev;
    }while( entry != first_entry );
}

/* ------------- Export Interfaces ------------- */

inline void LCacheAsyncDataFlush( LCache_t * theCache, void *p )
{
    // (!NVM_IsInOpenPR(p, 1)) return;

#if defined(CACHE_STATS)
    theCache->stats.async_calls ++;
#endif

    intptr_t cacheline = (intptr_t)p & DEFAULT_CACHE_LINE_MASK;
    intptr_t old_entry = LCacheOpEntrance( theCache, (intptr_t)p );

    if ( old_entry && old_entry != cacheline ) {
        //AO_nop_full();
#if defined(CACHE_DEBUG)
        fprintf(stderr, "[CACHE] old entry %p\n", old_entry);
#endif
        NVM_CLFLUSH( old_entry );
#if defined(CACHE_STATS)
	data_flushes++;
#endif
    }

#if defined(CACHE_TRACE)
    fprintf(stderr, "LCacheAsyncDataFlush, old : %p, new: %p, data flushes: %d, misses: %d\n", old_entry, cacheline, data_flushes, theCache->misses);
#endif
}

void LCacheAsyncMemOpDataFlush(LCache_t * theCache, void *dst, size_t sz)
{
    //if (!NVM_IsInOpenPR(dst, 1)) return;

    if (sz <= 0) return;

#if defined(CACHE_STATS)
	theCache->stats.async_memop_calls ++;    
#endif
    char *last_addr = (char*)dst + sz - 1;
    char *cacheline =
        (char*)(((uint64_t)dst) & DEFAULT_CACHE_LINE_MASK);
    char *last_cacheline =
        (char*)(((uint64_t)last_addr) & DEFAULT_CACHE_LINE_MASK);

    intptr_t old_entry;
    //AO_nop_full();
    do {
        old_entry = LCacheOpEntrance( theCache, (intptr_t)cacheline);
		
        if ( old_entry && old_entry != (intptr_t)cacheline) {
            NVM_CLFLUSH( old_entry );
#if defined(CACHE_STATS)
	    data_flushes++;
#endif
        }
#if defined(CACHE_TRACE)
    fprintf(stderr, "LCacheAsyncMemOpDataFlush, old : %p, new: %p, data flushes: %d, misses: %d\n", old_entry, cacheline, data_flushes, theCache->misses);
#endif
        cacheline += DEFAULT_CACHE_LINE_SIZE;
    }while (cacheline < last_cacheline + 1);
}

/** 
 *  This function acts as a break point of all-program non-volatile 
 *  memory store data stream. We have two methods to deal with this 
 *  case: 
 *  1. leave theCache->actual_size as is. only flush cache lines in
 *     the cache. this will cause:
 *     1) no hurt correctness; 2 ) impact performance; 3) miss stats less;
 *     This method corresponds to : view all non-volatile stores as
 *     a whole data stream to get cache stats, meaning looking at all
 *     accesses to the cache from whole-program point of view.
 *  2. set theCache->actual_size with 0 and clear the cache line contents
 *     with the initial values. This corresponds to : view all non-volatile 
 *     stores as many data streams splitted by FASEs, and obtain cache
 *     stats of each data stream, and then sum them. This one fits the
 *     real stuiation. (RECOMMENDED)
 */
void LCacheSyncDataFlush( LCache_t * theCache )
{
#if defined(CACHE_STATS)
    theCache->stats.sync_calls ++;
    theCache->stats.actual_sync_calls ++;
#endif

    //AO_nop_full();
    LCacheLine_t * tmp;
    LCacheLine_t * n;
    size_t i =0;
    list_for_each_entry_safe_cxx11( tmp, n, &theCache->the_cache, list ) {
	if ( tmp->cacheline ) {
            NVM_CLFLUSH( tmp->cacheline );
#if defined(CACHE_STATS)
            data_flushes += 1; 
	    theCache->stats.sync_data_flushes ++; 
#endif
	}
    }
#if defined(CACHE_DEBUG)
    fprintf(stderr, "data flushes: %d, misses: %d\n", data_flushes, theCache->misses);
    PrintStats(theCache);
#endif

#if defined(CACHE_SYNC_METHOD1)
    /* leave theCache->actual_size as is. */
#endif
#if defined(CACHE_SYNC_METHOD2) /* recommended */
    ResetCache( theCache );
#endif
    //AO_nop_full();
}

#endif /* _USE_SCACHE */

#endif /* _L_CACHE_H_ */
