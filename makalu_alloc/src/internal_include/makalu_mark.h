#ifndef _MAKALU_MARK_H
#define _MAKALU_MARK_H

MAK_EXTERN int MAK_all_interior_pointers;

MAK_INNER void MAK_init_persistent_roots();
MAK_INNER void MAK_init_object_map(ptr_t start);
MAK_INNER void MAK_initialize_offsets(void);
MAK_INNER void MAK_register_displacement_inner(size_t offset);
MAK_INNER void MAK_mark_init();
MAK_INNER void MAK_clear_hdr_marks(hdr *hhdr);

#ifdef MAK_THREADS
#   define OR_WORD(addr, bits) AO_or((volatile AO_t *)(addr), (AO_t)(bits))

MAK_INNER void MAK_help_marker(void);

#else // ! MAK_THREADS

#   define OR_WORD(addr, bits) (void)(*(addr) |= (bits))

#endif   //MAK_THREADS 


# define set_mark_bit_from_hdr(hhdr,n) \
              OR_WORD((hhdr)->hb_marks+divWORDSZ(n), (word)1 << modWORDSZ(n))

# define set_mark_bit_from_hdr_unsafe(hhdr, n) \
       { \
           word* addr = (hhdr)->hb_marks+divWORDSZ(n); \
           word bits = (word) 1 << modWORDSZ(n); \
           (*(addr) |= (bits)); \
       }

# define set_mark_bit_from_mark_bits(marks, n) \
       { \
           word* addr = (marks)+divWORDSZ(n); \
           word bits = (word) 1 << modWORDSZ(n); \
           (*(addr) |= (bits)); \
       }

# define mark_bit_from_hdr(hhdr,n) \
              (((hhdr)->hb_marks[divWORDSZ(n)] >> modWORDSZ(n)) & (word)1)

#define  mark_bit_from_mark_bits(marks, n) \
              ((marks[divWORDSZ(n)] >> modWORDSZ(n)) & (word)1)

# define clear_and_flush_mark_bit_from_hdr(hhdr, n, aflush_tb, aflush_tb_sz) \
       { \
           word* addr = (hhdr)->hb_marks+divWORDSZ(n); \
           word bits = ~ ((word) 1 << modWORDSZ(n)); \
           (*(addr) &= (bits)); \
           MAK_NVM_ASYNC_RANGE_VIA(addr, sizeof(word), \
                 aflush_tb, aflush_tb_sz); \
       }



#endif // _MAKALU_MARK_H
