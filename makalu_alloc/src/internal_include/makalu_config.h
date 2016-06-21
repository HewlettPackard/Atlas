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
#define MAK_API
#define MAK_INLINE static inline
#define STATIC static

//block
#define SYS_PAGESIZE 4096
#define CPP_LOG_HBLKSIZE 12
#define CPP_HBLKSIZE (1 << CPP_LOG_HBLKSIZE)
#define LOG_HBLKSIZE   ((size_t)CPP_LOG_HBLKSIZE)
#define HBLKSIZE ((size_t)CPP_HBLKSIZE)

#define MINHINCR 16   /* Minimum heap increment, in blocks of HBLKSIZE  */
                         /* Must be multiple of largest page size.         */
#define MAXHINCR 2048 /* Maximum heap increment, in blocks              */

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


//mark


//granule
#define CPP_WORDSZ 64
#define GRANULE_BYTES 16

#define MARK_BITS_PER_HBLK (HBLKSIZE/GRANULE_BYTES) 
  /* upper bound */

//struct hblkhdr hb_flags possible values

# define IGNORE_OFF_PAGE  1 /* Ignore pointers that do not  */
                            /* point to the first page of   */
                            /* this object.                 */
# define FREE_BLK 4 /* Block is free, i.e. not in use.      */


//struct hblkhdr page_reclaim_state possible values
#define IN_RECLAIMLIST 0
#define IN_FLOATING 1


//struct hblkhdr hb_marks
#define MARK_BITS_SZ (MARK_BITS_PER_HBLK/CPP_WORDSZ + 1)

#define MAK_INTERIOR_POINTERS 1


//headers
#define LOG_BOTTOM_SZ 10
#define BOTTOM_SZ (1 << LOG_BOTTOM_SZ)
#define LOG_TOP_SZ 11
#define TOP_SZ (1 << LOG_TOP_SZ)

#define HDR_CACHE_SIZE 8  /* power of 2 */
#define MAX_JUMP (HBLKSIZE - 1)
/* Is the result a forwarding address to someplace closer to the        */
/* beginning of the block or NULL?                                      */
#define IS_FORWARDING_ADDR_OR_NIL(hhdr) ((size_t) (hhdr) <= MAX_JUMP)

//heap sizes
#   define MAX_HEAP_SECTS 1024


//persistent

/* number of heap blocks allocated for logging in persistent memory */
#define N_PERSISTENT_LOG_HBLK 1

/*number of heap blocks allocated to store persistent roots */
#define N_PERSISTENT_ROOTS_HBLK 1

#define MAX_PERSISTENT_ROOTS_SPACE (HBLKSIZE * N_PERSISTENT_ROOTS_HBLK)

#define MAX_LOG_SZ  (HBLKSIZE * N_PERSISTENT_LOG_HBLK)

//#define NVM_DEBUG 1

//#define NO_CLFLUSH 1

//#define NO_NVM_LOGS 1


#define AFLUSH_TABLE_SZ 32     //multiples of 2
#define SFLUSH_TABLE_SZ 8
#define FL_AFLUSH_TABLE_SZ 8     //multiples of 2
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
