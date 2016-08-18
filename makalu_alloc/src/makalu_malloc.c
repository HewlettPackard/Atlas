#include "makalu_internal.h"


/* Fill in additional entries in MAK_size_map, including the ith one */
/* We assume the ith entry is currently 0.                              */
/* Note that a filled in section of the array ending at n always    */
/* has length at least n/4.                                             */
STATIC void MAK_extend_size_map(size_t i)
{
    size_t orig_granule_sz = ROUNDED_UP_GRANULES(i);
    size_t granule_sz = orig_granule_sz;
    size_t byte_sz = GRANULES_TO_BYTES(granule_sz);
                        /* The size we try to preserve.         */
                        /* Close to i, unless this would        */
                        /* introduce too many distinct sizes.   */
    size_t smaller_than_i = byte_sz - (byte_sz >> 3);
    size_t much_smaller_than_i = byte_sz - (byte_sz >> 2);
    size_t low_limit;   /* The lowest indexed entry we  */
                        /* initialize.                  */
    size_t j;

    if (MAK_size_map[smaller_than_i] == 0) {
        low_limit = much_smaller_than_i;
        while (MAK_size_map[low_limit] != 0) low_limit++;
    } else {
        low_limit = smaller_than_i + 1;
        while (MAK_size_map[low_limit] != 0) low_limit++;
        granule_sz = ROUNDED_UP_GRANULES(low_limit);
        granule_sz += granule_sz >> 3;
        if (granule_sz < orig_granule_sz) granule_sz = orig_granule_sz;
    }
    /* For these larger sizes, we use an even number of granules.       */
    /* This makes it easier to, for example, construct a 16byte-aligned */
    /* allocator even if GRANULE_BYTES is 8.                            */
        granule_sz += 1;
        granule_sz &= ~1;
    if (granule_sz > MAXOBJGRANULES) {
        granule_sz = MAXOBJGRANULES;
    }
    /* If we can fit the same number of larger objects in a block,      */
    /* do so.                                                   */
    {
        size_t number_of_objs = HBLK_GRANULES/granule_sz;
        granule_sz = HBLK_GRANULES/number_of_objs;
        granule_sz &= ~1;
    }
    byte_sz = GRANULES_TO_BYTES(granule_sz);
    /* We may need one extra byte;                      */
    /* don't always fill in MAK_size_map[byte_sz]        */
    byte_sz -= EXTRA_BYTES;

    for (j = low_limit; j <= byte_sz; j++) MAK_size_map[j] = granule_sz;
}



#ifdef MAK_THREADS
MAK_INNER void * MAK_core_malloc(size_t lb)
#else
MAK_API void * MAK_CALL MAK_malloc(size_t lb)
#endif
{
    fl_hdr* flh;
    signed_word fl_count;
    word start_idx;
    void **flp;
    size_t lg;
    void *result;
    
    
    if(SMALL_OBJ(lb)) {
        lg = MAK_size_map[lb];
        flh = &(MAK_objfreelist[lg]);
        word max_count = MAK_fl_max_count[lg];
        MAK_LOCK_GRAN(lg);
        fl_count = flh -> count;
        if (EXPECT(fl_count == 0, FALSE)){
              MAK_UNLOCK_GRAN(lg);
              return (MAK_generic_malloc((word) lb, NORMAL));
        }
        flp = flh -> fl;
        start_idx = flh -> start_idx;
        signed_word new_count = fl_count - 1;
        word idx = (start_idx + new_count) % max_count;
        result = (void*) (flp[idx]);
        flh -> count = new_count;
        MAK_UNLOCK_GRAN(lg);
        PREFETCH_FOR_WRITE(result);
        return result;
     } else {
          return(MAK_generic_malloc(lb, NORMAL));
     }
}

MAK_INNER void * MAK_generic_malloc(size_t lb, int k)
{
    void *op = NULL;
    if(SMALL_OBJ(lb)) {
        struct obj_kind * kind = MAK_obj_kinds + k;
        size_t lg = MAK_size_map[lb];
        if (lg == 0){ //check if we are in the right granule
            MAK_LOCK();
            //check again holding lock
            if (MAK_size_map[lb] == 0){
                if (!MAK_is_initialized) 
                    ABORT("Makalu not properly initialized!");
                MAK_extend_size_map(lb);
            }
            MAK_UNLOCK();
            lg = MAK_size_map[lb];
        }
        fl_hdr* flh = &(kind -> ok_freelist[lg]);
        MAK_LOCK_GRAN(lg);
        if (flh -> fl == 0){
            MAK_LOCK();
            flh -> fl = (void**) MAK_transient_scratch_alloc(
            MAK_fl_max_count[lg] * sizeof(void*));
            if (MAK_reclaim_list[k] == 0){
                if (!MAK_alloc_reclaim_list(k)){
                    ABORT("Cannot allocate transient memory for reclaim");
                }
                MAK_STORE_NVM_SYNC(kind->ok_seen, TRUE);
            }
            MAK_UNLOCK()
        }
        op = MAK_allocobj(lg, k);
        if (op == 0){
            MAK_UNLOCK_GRAN(lg);
            return op;
        }
        flh -> count = flh -> count - 1;
        MAK_UNLOCK_GRAN(lg);
    } else {    //LARGE OBJECT
        size_t lg;
        size_t lb_rounded;
        word n_blocks;
        MAK_bool init;
        lg = ROUNDED_UP_GRANULES(lb);
        lb_rounded = GRANULES_TO_BYTES(lg);
        n_blocks = OBJ_SZ_TO_BLOCKS(lb_rounded);
        init = MAK_obj_kinds[k].ok_init;
        MAK_LOCK();
        op = (ptr_t)MAK_alloc_large(lb_rounded, k, 0);
        if (0 != op) {
#ifdef MAK_THREADS
         /* Clear any memory that might be used for GC descriptors */
         /* before we release the lock.                            */
            ((word *)op)[0] = 0;
            ((word *)op)[1] = 0;
            ((word *)op)[GRANULES_TO_WORDS(lg)-1] = 0;
            ((word *)op)[GRANULES_TO_WORDS(lg)-2] = 0;
#endif   
        }
        MAK_UNLOCK();
        if (init && 0 != op) {
            BZERO(op, n_blocks * HBLKSIZE);
        }
    }
    return op;
}


/* Allocate a large block of size lb bytes.     */
/* The block is not cleared.                    */
/* Flags is 0 or IGNORE_OFF_PAGE.               */
/* We hold the allocation lock.                 */
/* EXTRA_BYTES were already added to lb.        */
MAK_INNER ptr_t MAK_alloc_large(size_t lb, int k, unsigned flags)
{
    struct hblk * h;
    word n_blocks;
    ptr_t result;
    MAK_bool retry = FALSE;

    /* Round up to a multiple of a granule. */
    lb = (lb + GRANULE_BYTES - 1) & ~(GRANULE_BYTES - 1);
    n_blocks = OBJ_SZ_TO_BLOCKS(lb);

    if (!MAK_is_initialized)
        ABORT("Makalu not properly initialized");

    h = MAK_allochblk(lb, k, flags);

    while (0 == h && MAK_try_expand_hp(n_blocks, flags != 0, retry)) {
        h = MAK_allochblk(lb, k, flags);
        retry = TRUE;
    }
    if (h == 0) {
        result = 0;
    } else {
        result = h -> hb_body;
    }
    return result;
}



/*
 * Make sure the object free list for size gran (in granules) is not empty.
 * Return a pointer to the first object on the free list.
 * The object MUST BE REMOVED FROM THE FREE LIST BY THE CALLER.
 * Assumes we hold the allocator lock.
 */

MAK_INNER ptr_t MAK_allocobj(size_t gran, int kind)
{
    fl_hdr* flh = &(MAK_obj_kinds[kind].ok_freelist[gran]);
    MAK_bool retry = FALSE;

    if (gran == 0) return(0);

    if (flh -> count <= 0) {
        /* Sweep blocks for objects of this size */
        MAK_continue_reclaim(gran, kind);
        //the calling method decrements the count of memory objects in the freelist
        flh -> start_idx = 0;
    }

    if (flh -> count <= 0)
    {
        MAK_LOCK()
        while (flh -> count <= 0)
        {
            MAK_new_hblk(gran, kind);
            if (flh -> count <= 0
                && !MAK_try_expand_hp(1, FALSE, retry))
            {
                MAK_printf("Warning: Could not expand heap\n");
                break;
            }
        }
        MAK_UNLOCK();
        //the calling method decrements the count of memory objects in the freelist
        flh -> start_idx = 0;
    }
    if (flh -> count <= 0){
        return (0);
    }

    signed_word new_count = flh -> count - 1;
    word idx = (flh -> start_idx  + new_count) % MAK_fl_max_count[gran];
    return (flh -> fl[idx]);
}

#ifdef MAK_THREADS
MAK_INNER void MAK_core_free(void* p, hdr* hhdr, int knd, 
    size_t sz, size_t ngranules){

    fl_hdr* flh;
    struct obj_kind * ok;
    struct hblk* h = hhdr -> hb_block;

    ok = &MAK_obj_kinds[knd];
    if (EXPECT(ngranules <= MAXOBJGRANULES, TRUE)) {
        if (ok -> ok_init) {
            BZERO((word *)p, sz);
        }
        flh = &(ok -> ok_freelist[ngranules]);

        MAK_LOCK_GRAN(ngranules);
        signed_word count = flh -> count;
        word max_count = MAK_fl_max_count[ngranules];
        word idx = (flh -> start_idx + count) % max_count;
        void** flp = flh -> fl;
        flp[idx] = p;
        count = flh -> count = count + 1;
        //also now we are addding to the global freelist, 
        //cache the header in the global cache as well.
        MAK_update_hc(h -> hb_body, hhdr, MAK_hdr_cache, HDR_CACHE_SIZE);
        if (count >= max_count){
            //TODO:truncate
            MAK_truncate_freelist(flh, ngranules, knd,
                 MAK_fl_optimal_count[ngranules], max_count,
                 &(MAK_hdr_cache[0]), (word) HDR_CACHE_SIZE,
                 &(MAK_fl_aflush_table[0]), (word) FL_AFLUSH_TABLE_SZ);

        }
        MAK_UNLOCK_GRAN(ngranules);
    } else {
        MAK_LOCK();
        MAK_START_NVM_ATOMIC;

        MAK_freehblk(h);

        MAK_END_NVM_ATOMIC;
        MAK_UNLOCK();
    }
}

#else // ! defined MAK_THREADS
MAK_API void MAK_CALL MAK_free(void * p)
{
    hdr *hhdr;
    size_t sz; /* In bytes */
    size_t ngranules;   /* sz in granules */
    fl_hdr *flh;
    int knd;
    struct obj_kind * ok;

    if (p == 0) return;
        /* Required by ANSI.  It's not my fault ...     */
    MAK_LOCK();
    void* r = MAK_hc_base_with_hdr(p, MAK_hdr_cache,
                     (unsigned int) HDR_CACHE_SIZE, &hhdr);
    if (r == 0) return;

    sz = hhdr -> hb_sz;
    ngranules = BYTES_TO_GRANULES(sz);
    knd = hhdr -> hb_obj_kind;
    ok = &MAK_obj_kinds[knd];
    if (EXPECT(ngranules <= MAXOBJGRANULES, TRUE)) {
        if (ok -> ok_init) {
            BZERO((word *)r, sz);
        }
        flh = &(ok -> ok_freelist[ngranules]);
        signed_word count = flh -> count;
        word max_count = MAK_fl_max_count[ngranules];
        word idx = (flh -> start_idx + count) % max_count;
        void** flp = flh -> fl;
        flp[idx] = r;
        count = flh -> count = count + 1;
        if (count >= max_count){
           MAK_truncate_freelist(flh, ngranules, knd,
              MAK_fl_optimal_count[ngranules], max_count,
              &(MAK_hdr_cache[0]), (word) HDR_CACHE_SIZE,
              &(MAK_fl_aflush_table[0]), (word) FL_AFLUSH_TABLE_SZ);
        }
        MAK_UNLOCK();
    } else {
        MAK_START_NVM_ATOMIC;
        MAK_freehblk(hhdr -> hb_block);
        MAK_END_NVM_ATOMIC;
        MAK_UNLOCK();
    }
}


#endif // MAK_THREADS


