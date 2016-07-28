#ifndef _MAKALU_MARK_H
#define _MAKALU_MARK_H

MAK_EXTERN int MAK_all_interior_pointers;


MAK_INNER void MAK_init_persistent_roots();
MAK_INNER void MAK_init_object_map(ptr_t start);
MAK_INNER void MAK_initialize_offsets(void);
MAK_INNER void MAK_register_displacement_inner(size_t offset);

#endif
