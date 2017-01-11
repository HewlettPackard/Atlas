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

#ifndef _MAKALU_H
#define _MAKALU_H

#define MAK_API 
#define MAK_CALL 
#define MAK_CALLBACK MAK_CALL

#include <stddef.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif


typedef unsigned long long MAK_word;
typedef long long MAK_signed_word;

/* start/restart */
typedef int (MAK_CALLBACK *MAK_persistent_memalign)(void** memptr,
              size_t alignment, size_t size);

MAK_API void* MAK_CALL MAK_start(MAK_persistent_memalign funcptr);
MAK_API void MAK_CALL MAK_restart(char* start_addr, MAK_persistent_memalign funcptr);
MAK_API void MAK_CALL MAK_start_off(char* start_addr,
              MAK_persistent_memalign funcptr);

MAK_API void MAK_CALL MAK_close(void);

/* malloc/free */

MAK_API void* MAK_CALL MAK_malloc(size_t /*size in bytes */);
MAK_API void MAK_CALL MAK_free(void * /*pointer */);

/* pthread */
# define MAK_PTHREAD_CREATE_CONST const
#define MAK_PTHREAD_EXIT_ATTRIBUTE __attribute__((__noreturn__))

MAK_API int MAK_pthread_create(pthread_t *,
                             MAK_PTHREAD_CREATE_CONST pthread_attr_t *,
                             void *(*)(void *), void * /* arg */);
MAK_API int MAK_pthread_join(pthread_t, void ** /* retval */);
MAK_API int MAK_pthread_detach(pthread_t);
MAK_API int MAK_pthread_cancel(pthread_t);
MAK_API void MAK_pthread_exit(void *) MAK_PTHREAD_EXIT_ATTRIBUTE;

typedef void (MAK_CALLBACK * MAK_fas_free_callback)(pthread_t, void*);
MAK_API void MAK_CALL MAK_set_defer_free_fn(MAK_fas_free_callback);
MAK_API void MAK_CALL MAK_free_imm(void *);

/* 1: indicates that the calling thread will likely never allocate */
/* 0: default, likely alloocates */
MAK_API void MAK_CALL MAK_declare_never_allocate(int /*flag */);

/* persistent root */

MAK_API void** MAK_CALL MAK_persistent_root_addr(unsigned int id);
MAK_API void* MAK_CALL MAK_persistent_root(unsigned int id);
MAK_API void MAK_CALL MAK_set_persistent_root(unsigned int id, void* val);

/* gc */
MAK_API int MAK_CALL MAK_collect_off(void);

/* shutdown */
MAK_API void MAK_CALL MAK_close(void);


#ifdef __cplusplus
}
#endif



#endif
