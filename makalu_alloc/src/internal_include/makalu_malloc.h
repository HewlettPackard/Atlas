#ifndef _MAKALU_MALLOC_H
#define _MAKALU_MALLOC_H

MAK_INNER void MAK_generic_malloc_many(size_t lb, int k, fl_hdr* flh,
        hdr_cache_entry* hc,
        word hc_sz,
        void** aflush_tb,
        word aflush_tb_sz);

MAK_INNER void * MAK_core_malloc(size_t);
MAK_INNER void * MAK_generic_malloc(size_t lb, int k);
MAK_INNER ptr_t MAK_alloc_large(size_t lb, int k, unsigned flags);
MAK_INNER ptr_t MAK_allocobj(size_t gran, int kind);
MAK_INNER void MAK_core_free(void* p, hdr* hhdr, int knd, 
     size_t sz, size_t ngranules);

MAK_INNER void MAK_generic_malloc_many(size_t lb, int k, fl_hdr* flh,
                                  hdr_cache_entry* hc,
                                  word hc_sz,
                                  void** aflush_tb,
                                  word aflush_tb_sz);

#endif
