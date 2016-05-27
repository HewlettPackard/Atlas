#ifndef _MAKALU_CONFIG_H
#define _MAKALU_CONFIG_H

# include "atomic_ops.h"



//typedefs
typedef unsigned long long word;
typedef long long signed_word;
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

//block
#define CPP_LOG_HBLKSIZE 12
#define CPP_HBLKSIZE (1 << CPP_LOG_HBLKSIZE)
#define LOG_HBLKSIZE   ((size_t)CPP_LOG_HBLKSIZE)
#define HBLKSIZE ((size_t)CPP_HBLKSIZE)


//mark

MAK_EXTERN MAK_all_interior_pointers;

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


//headers
#define LOG_BOTTOM_SZ 10
#define BOTTOM_SZ (1 << LOG_BOTTOM_SZ)
#define LOG_TOP_SZ 11
#define TOP_SZ (1 << LOG_TOP_SZ)

#define HDR_CACHE_SIZE 8  /* power of 2 */
#define MAX_JUMP (HBLKSIZE - 1)

//heap sizes
#   define MAX_HEAP_SECTS 1024

#endif
