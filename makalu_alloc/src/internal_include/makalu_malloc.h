#ifndef _MAKALU_MALLOC_H
#define _MAKALU_MALLOC_H

MAK_INNER void * MAK_core_malloc(size_t);
MAK_INNER void * MAK_generic_malloc(size_t lb, int k);
MAK_INNER ptr_t MAK_alloc_large(size_t lb, int k, unsigned flags);
MAK_INNER ptr_t MAK_allocobj(size_t gran, int kind);


#endif
