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
 *
 * Source code is partially derived from Boehm-Demers-Weiser Garbage 
 * Collector (BDWGC) version 7.2 (license is attached)
 *
 * File:
 *   gc_priv.h
 *
 *   Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 *   Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
 *   Copyright (c) 1996-1999 by Silicon Graphics.  All rights reserved.
 *   Copyright (c) 1999-2004 Hewlett-Packard Development Company, L.P.
 *
 *
 *   THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY EXPRESSED
 *   OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.
 *
 *   Permission is hereby granted to use or copy this program
 *   for any purpose,  provided the above notices are retained on all copies.
 *   Permission to modify the code and to distribute modified code is granted,
 *   provided the above notices are retained, and a notice that the code was
 *   modified is included with the above copyright notice.
 */

#ifndef _MAKALU_CONFIG_H
#define _MAKALU_CONFIG_H

# include "atomic_ops.h"
# include "stdlib.h"

#include "makalu.h"



//typedefs
typedef MAK_word word;
typedef MAK_signed_word signed_word;
typedef AO_t counter_t;

typedef int MAK_bool;
#define TRUE 1
#define FALSE 0

typedef char * ptr_t;   /* A generic pointer to which we can add        */
                        /* byte displacements and which can be used     */
                        /* for address comparisons. */

//code visibility
#define MAK_INNER 
#define MAK_EXTERN extern MAK_INNER
#define MAK_INLINE static inline
#define STATIC static

//run modes
#define RESTARTING_OFFLINE 10
#define RESTARTING_ONLINE  11
#define STARTING_ONLINE    12

// multi-threaded support
#define MAK_THREADS 1
#define START_THREAD_LOCAL_IMMEDIATE 1
/* if this flag is commented out */
/* we don't start local allocation until we have allocated */
/* optimal count worth of memory from global freelist */




//block

#define SYS_PAGESIZE 4096
#define CPP_LOG_HBLKSIZE 12
#define CPP_HBLKSIZE (1 << CPP_LOG_HBLKSIZE)
#define LOG_HBLKSIZE   ((size_t)CPP_LOG_HBLKSIZE)
#define HBLKSIZE ((size_t)CPP_HBLKSIZE)
#define HBLKDISPL(objptr) (((size_t) (objptr)) & (HBLKSIZE-1))
#define MAK_FREE_SPACE_DIVISOR 3

#define CPP_MAXOBJBYTES (CPP_HBLKSIZE/2)
#define MAXOBJBYTES ((size_t)CPP_MAXOBJBYTES)
#define CPP_MAXOBJGRANULES BYTES_TO_GRANULES(CPP_MAXOBJBYTES)
#define MAXOBJGRANULES ((size_t)CPP_MAXOBJGRANULES)

#define MINHINCR 16   /* Minimum heap increment, in blocks of HBLKSIZE  */
                         /* Must be multiple of largest page size.         */
#define MAXHINCR 2048 /* Maximum heap increment, in blocks              */

#define MAXOBJBYTES ((size_t)CPP_MAXOBJBYTES)

# define divHBLKSZ(n) ((n) >> LOG_HBLKSIZE)

# define HBLK_PTR_DIFF(p,q) divHBLKSZ((ptr_t)p - (ptr_t)q)
        /* Equivalent to subtracting 2 hblk pointers.   */
        /* We do it this way because a compiler should  */
        /* find it hard to use an integer division      */
        /* instead of a shift.  The bundled SunOS 4.1   */
        /* o.w. sometimes pessimizes the subtraction to */
        /* involve a call to .div.                      */


# define OBJ_SZ_TO_BLOCKS(sz) divHBLKSZ((sz) + HBLKSIZE-1)
    /* Size of block (in units of HBLKSIZE) needed to hold objects of   */
    /* given sz (in bytes).   */

# define HBLKPTR(objptr) ((struct hblk *)(((word) (objptr)) & ~(HBLKSIZE-1)))

#define UNIQUE_THRESHOLD 32
/* Sizes up to this many HBLKs each have their own free list    */

# define FL_COMPRESSION 8
        /* In between sizes map this many distinct sizes to a single    */
        /* bin.                                                         */
# define HUGE_THRESHOLD 256
        /* Sizes of at least this many heap blocks are mapped to a      */
        /* single free list.                                            */

# define N_HBLK_FLS (HUGE_THRESHOLD - UNIQUE_THRESHOLD)/FL_COMPRESSION \
                                 + UNIQUE_THRESHOLD

/* The number of objects in a block dedicated to a certain size.        */
/* may erroneously yield zero (instead of one) for large objects.       */
# define HBLK_OBJS(sz_in_bytes) (HBLKSIZE/(sz_in_bytes))


#define BLOCK_NEARLY_FULL_THRESHOLD(size_in_bytes) (7 * HBLK_OBJS(size_in_bytes) / 8)

//granule
#define CPP_WORDSZ 64
#  define LOGWL  ((word)6)    /* log[2] of CPP_WORDSZ */
#define WORDSZ ((word)CPP_WORDSZ)
#define SIGNB  ((word)1 << (WORDSZ-1))
#define GRANULE_BYTES 16
#define GRANULE_WORDS 2
#define RAW_BYTES_FROM_INDEX(i) ((i) * GRANULE_BYTES)
#define BYTES_TO_GRANULES(n) ((n)>>4)
#define GRANULES_TO_BYTES(n) ((n)<<4)
#define GRANULES_TO_WORDS(n) ((n)<<1)
#define WORDS_TO_BYTES(x)   ((x)<<3)
#define BYTES_TO_WORDS(x)   ((x)>>3)

#define BYTES_PER_WORD      ((word)(sizeof (word)))

#define ONES ((word)(signed_word)(-1))

# define ROUNDED_UP_GRANULES(n) \
        BYTES_TO_GRANULES((n) + (GRANULE_BYTES - 1 + EXTRA_BYTES))

#define divWORDSZ(n) ((n) >> LOGWL)     /* divide n by size of word */
#  define modWORDSZ(n) ((n) & 0x3f)        /* n mod size of word            */

# define HBLK_GRANULES (HBLKSIZE/GRANULE_BYTES)

//mark
//#define POINTER_MASK
//#define POINTER_SHIFT

#define MAP_LEN BYTES_TO_GRANULES(HBLKSIZE)

#define MARK_BITS_PER_HBLK (HBLKSIZE/GRANULE_BYTES) 
  /* upper bound */

#  define FINAL_MARK_BIT(sz) \
           ((sz) > MAXOBJBYTES ? MARK_BITS_PER_HBLK \
                : BYTES_TO_GRANULES((sz) * HBLK_OBJS(sz)))

//struct hblkhdr hb_marks
#define MARK_BITS_SZ (MARK_BITS_PER_HBLK/CPP_WORDSZ + 1)
#  define MARK_BIT_NO(offset, sz) BYTES_TO_GRANULES((unsigned)(offset))
#  define MARK_BIT_OFFSET(sz) BYTES_TO_GRANULES(sz)

#define MAK_INTERIOR_POINTERS 0 /* 1 */


/*number of heap blocks allocated to store persistent roots */
#define N_PERSISTENT_ROOTS_HBLK 1 
#define MAX_PERSISTENT_ROOTS_SPACE (HBLKSIZE * N_PERSISTENT_ROOTS_HBLK)

/* Object descriptors on mark stack or in objects.  Low order two       */
/* bits are tags distinguishing among the following 4 possibilities     */
/* for the high order 30 bits.                                          */
#define MAK_DS_TAG_BITS 2
#define MAK_DS_TAGS   ((1 << MAK_DS_TAG_BITS) - 1)
#define MAK_DS_LENGTH 0  /* The entire word is a length in bytes that    */
                        /* must be a multiple of 4.                     */
#define MAK_DS_BITMAP 1  /* 30 (62) bits are a bitmap describing pointer */
                        /* fields.  The msb is 1 if the first word      */
                        /* is a pointer.                                */
                        /* (This unconventional ordering sometimes      */
                        /* makes the marker slightly faster.)           */
                        /* Zeroes indicate definite nonpointers.  Ones  */
                        /* indicate possible pointers.                  */
                        /* Only usable if pointers are word aligned.    */
#define MAK_DS_PROC   2
                        /* The objects referenced by this object can be */
                        /* pushed on the mark stack by invoking         */
                        /* PROC(descr).  ENV(descr) is passed as the    */
                        /* last argument.                               */
#define MAK_MAKE_PROC(proc_index, env) \
            (((((env) << MAK_LOG_MAX_MARK_PROCS) \
               | (proc_index)) << MAK_DS_TAG_BITS) | MAK_DS_PROC)
#define MAK_DS_PER_OBJECT 3  /* The real descriptor is at the            */
                        /* byte displacement from the beginning of the  */
                        /* object given by descr & ~DS_TAGS             */
                        /* If the descriptor is negative, the real      */
                        /* descriptor is at (*<object_start>) -         */
                        /* (descr & ~DS_TAGS) - MAK_INDIR_PER_OBJ_BIAS   */
                        /* The latter alternative can be used if each   */
                        /* object contains a type descriptor in the     */
                        /* first word.                                  */
                        /* Note that in the multi-threaded environments */
                        /* per-object descriptors must be located in    */
                        /* either the first two or last two words of    */
                        /* the object, since only those are guaranteed  */
                        /* to be cleared while the allocation lock is   */
                        /* held.                                        */
#define MAK_INDIR_PER_OBJ_BIAS 0x10

# define VALID_OFFSET_SZ HBLKSIZE
#define MAK_N_MARKERS 6    /* Number of threads to be started */
                           /* for parallel marking offline    */

#define INITIAL_MARK_STACK_SIZE (1*HBLKSIZE)
                /* INITIAL_MARK_STACK_SIZE * sizeof(mse) should be a    */
                /* multiple of HBLKSIZE.                                */

#define LOCAL_MARK_STACK_SIZE HBLKSIZE
        /* Under normal circumstances, this is big enough to guarantee  */
        /* We don't overflow half of it in a single call to             */
        /* MAK_mark_from.                                                */



//allocation

#define ALIGNMENT 8

#define FL_MAX_SIZE 2 * HBLKSIZE
#define FL_OPTIMAL_SIZE 1 * HBLKSIZE
#define LOCAL_FL_MAX_SIZE 2 * HBLKSIZE   /* thread local */
#define LOCAL_FL_OPTIMAL_SIZE 1 * HBLKSIZE /* thread local */

/* upto and including 256 bytes in thread local freelist */
/* Note there is a 0 byte freelist */ 
#define TINY_FREELISTS 25   

//obj kinds
#define PTRFREE 0
#define NORMAL  1


#define MAK_N_KINDS_INITIAL_VALUE 2

#define MAXOBJKINDS 16

# define EXTRA_BYTES MAK_all_interior_pointers
# define MAX_EXTRA_BYTES 1
# if MAX_EXTRA_BYTES == 0
#  define SMALL_OBJ(bytes) EXPECT((bytes) <= (MAXOBJBYTES), TRUE)
# else
#  define SMALL_OBJ(bytes) \
            (EXPECT((bytes) <= (MAXOBJBYTES - MAX_EXTRA_BYTES), TRUE) \
             || (bytes) <= MAXOBJBYTES - EXTRA_BYTES)
        /* This really just tests bytes <= MAXOBJBYTES - EXTRA_BYTES.   */
        /* But we try to avoid looking up EXTRA_BYTES.                  */
# endif

# define ADD_SLOP(bytes) ((bytes) + EXTRA_BYTES)


//headers
#define LOG_BOTTOM_SZ 10
#define BOTTOM_SZ (1 << LOG_BOTTOM_SZ)
#define LOG_TOP_SZ 11
#define TOP_SZ (1 << LOG_TOP_SZ)

#define HDR_CACHE_SIZE 8  /* power of 2 */
#define LOCAL_HDR_CACHE_SZ 8 /* power of 2 */
#define MAX_JUMP (HBLKSIZE - 1)
/* Is the result a forwarding address to someplace closer to the        */
/* beginning of the block or NULL?                                      */
#define IS_FORWARDING_ADDR_OR_NIL(hhdr) ((size_t) (hhdr) <= MAX_JUMP)

//struct hblkhdr hb_flags possible values

# define IGNORE_OFF_PAGE  1 /* Ignore pointers that do not  */
                            /* point to the first page of   */
                            /* this object.                 */
# define FREE_BLK 4 /* Block is free, i.e. not in use.      */


//struct hblkhdr page_reclaim_state possible values
#define IN_RECLAIMLIST 0
#define IN_FLOATING 1


//heap sizes
#   define MAX_HEAP_SECTS 1024


//persistent


/* provide deferred deallocation for */
/* transaction based failure atomic sections */
//#define FAS_SUPPORT 1

/* number of heap blocks allocated for logging in persistent memory */
#define N_PERSISTENT_LOG_HBLK 1

/*number of heap blocks allocated to store persistent roots */
#define N_PERSISTENT_ROOTS_HBLK 1

#define MAX_PERSISTENT_ROOTS_SPACE (HBLKSIZE * N_PERSISTENT_ROOTS_HBLK)

#define MAX_LOG_SZ  (HBLKSIZE * N_PERSISTENT_LOG_HBLK)

//#define NVM_DEBUG 1

//#define NO_CLFLUSH 1

//#define NO_NVM_LOGS 1


#define AFLUSH_TABLE_SZ 32    /* power of 2 */ 
#define LOCAL_AFLUSH_TABLE_SZ 8 /* power of 2 */
#define SFLUSH_TABLE_SZ 8
#define FL_AFLUSH_TABLE_SZ 8   /* power of 2 */ 
#define CACHE_LINE_SZ 64
#define LOG_CACHE_LINE_SZ 6


# define INTEGER 1
# define CHAR 3
# define ADDR 4
# define WORD 5

#define MAGIC_NUMBER 45312

#define PERSISTENT_STATE_NONE 0
#define PERSISTENT_STATE_INCONSISTENT 2


#endif
