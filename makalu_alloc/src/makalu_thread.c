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
 *   pthread_support.c
 *   pthread_start.c
 *
 *   Copyright (c) 1994 by Xerox Corporation.  All rights reserved.
 *   Copyright (c) 1996 by Silicon Graphics.  All rights reserved.
 *   Copyright (c) 1998 by Fergus Henderson.  All rights reserved.
 *   Copyright (c) 2000-2005 by Hewlett-Packard Company.  All rights reserved.
 *
 *   THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY EXPRESSED
 *   OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.
 *
 *   Permission is hereby granted to use or copy this program
 *   for any purpose,  provided the above notices are retained on all copies.
 *   Permission to modify the code and to distribute modified code is granted,
 *   provided the above notices are retained, and a notice that the code was
 *   modified is included with the above copyright notice.
 *
 */

#include "makalu_internal.h"
#include "makalu_local_heap.h"

#include <semaphore.h>
#include <pthread.h>
#include <errno.h>

#ifdef MAK_THREADS

MAK_INNER pthread_mutex_t MAK_global_ml = PTHREAD_MUTEX_INITIALIZER;
STATIC pthread_mutex_t MAK_granule_ml[TINY_FREELISTS];

STATIC pthread_mutex_t mark_mutex = PTHREAD_MUTEX_INITIALIZER;
STATIC pthread_cond_t mark_cv = PTHREAD_COND_INITIALIZER;

MAK_INNER long MAK_n_markers = MAK_N_MARKERS;
MAK_INNER MAK_bool MAK_parallel_mark = FALSE;

MAK_INNER void MAK_lock_gran(word gran)
{
    int g = gran >= TINY_FREELISTS ? 0 : gran;
    pthread_mutex_lock(&(MAK_granule_ml[g]));
}

MAK_INNER void MAK_unlock_gran(word gran){
    int g = gran >= TINY_FREELISTS ? 0 : gran;
    pthread_mutex_unlock(&(MAK_granule_ml[g]));
}

MAK_INNER void MAK_acquire_mark_lock(void)
{
    pthread_mutex_lock(&(mark_mutex));
}

MAK_INNER void MAK_release_mark_lock(void)
{
    if (pthread_mutex_unlock(&mark_mutex) != 0) {
        MAK_abort("pthread_mutex_unlock failed");
    }
}

MAK_INNER void MAK_wait_marker(void)
{
    if (pthread_cond_wait(&mark_cv, &mark_mutex) != 0) {
        MAK_abort("pthread_cond_wait failed");
    }
}

MAK_INNER void MAK_notify_all_marker(void)
{
    if (pthread_cond_broadcast(&mark_cv) != 0) {
        MAK_abort("pthread_cond_broadcast failed");
    }
}

STATIC pthread_t MAK_mark_threads[MAK_N_MARKERS];

STATIC void * MAK_mark_thread(void * id)
{
    int cancel_state;
    if ((word)id == (word)-1) return 0; /* to make compiler happy */
    DISABLE_CANCEL(cancel_state);
    for (;;) {
        MAK_help_marker();
    }
}


MAK_INNER void MAK_start_mark_threads(void)
{

    if ( MAK_n_markers <= 1) {
        MAK_parallel_mark = FALSE;
        return;
    } else {
        MAK_parallel_mark = TRUE;
    }

    int i;
    pthread_attr_t attr;
    if (0 != pthread_attr_init(&attr)) 
        ABORT("pthread_attr_init failed");
    
    if (0 != pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
        ABORT("pthread_attr_setdetachstate failed");
    
    for (i = 0; i < MAK_n_markers - 1; ++i) {
        if (0 != REAL_FUNC(pthread_create)(MAK_mark_threads + i, &attr,
                              MAK_mark_thread, (void *)(word)i)) {
            WARN("Marker thread creation failed", 0);
            /* Don't try to create other marker threads.    */
            MAK_n_markers = i + 1;
            if (i == 0) MAK_parallel_mark = FALSE;
            break;
        }
    }
    pthread_attr_destroy(&attr);
}

MAK_INNER void MAK_thr_init(void)
{
    int i;
    for (i=0; i < TINY_FREELISTS; i++)
    {
        pthread_mutex_init(MAK_granule_ml + i, NULL);
    }
    
    MAK_init_thread_local();    
    MAK_set_my_thread_local();

}

struct start_info {
    void *(*start_routine)(void *);
    void *arg;
    sem_t registered;           /* 1 ==> in our thread table, but       */
                                /* parent hasn't yet noticed.           */
};


STATIC void MAK_thread_exit_proc(void *arg)
{
    NEED_CANCEL_STATE(int cancel_state;);
    DISABLE_CANCEL(cancel_state);
    MAK_teardown_thread_local((MAK_tlfs) arg);
    RESTORE_CANCEL(cancel_state);
    MAK_ACCUMULATE_FLUSH_COUNT();
}

STATIC void* MAK_start_routine(void* arg)
{
    void * result;
    void* (*pstart)(void*);
    struct start_info * si;
    void* pstart_arg;


    si = arg;
    pstart = si -> start_routine;
    pstart_arg = si -> arg;

    sem_post(&(si -> registered));
    /* Last action on si.   */

    MAK_tlfs tlfs = MAK_set_my_thread_local();
    
    pthread_cleanup_push(MAK_thread_exit_proc, tlfs);

    result = (*pstart)(pstart_arg);
    
    pthread_cleanup_pop(1);

    return result; 
}


MAK_API int WRAP_FUNC(pthread_create)(pthread_t *new_thread,
                     MAK_PTHREAD_CREATE_CONST pthread_attr_t *attr,
                     void *(*start_routine)(void *), void *arg)
{

    int result;
    struct start_info si;

    if (!MAK_is_initialized) {
        ABORT("Makalu not initialized properly before starting threads!\n");
    }
    
    if (sem_init(&(si.registered), MAK_SEM_INIT_PSHARED, 0) != 0)
        ABORT("sem_init failed");

    si.start_routine = start_routine;
    si.arg = arg;

    result = REAL_FUNC(pthread_create)(new_thread, attr, MAK_start_routine, &si);

    /* Wait until the child is done with the start info */
    if (0 == result) {
        NEED_CANCEL_STATE(int cancel_state;)
        DISABLE_CANCEL(cancel_state);
        while (0 != sem_wait(&(si.registered))){
            if (EINTR != errno) ABORT("sem_wait failed");
        }
        RESTORE_CANCEL(cancel_state);
    }
    sem_destroy(&(si.registered));
    return(result);
}

MAK_API int MAK_pthread_join(pthread_t thread, void **retval)
{
    return REAL_FUNC(pthread_join)(thread, retval);
}

MAK_API int WRAP_FUNC(pthread_cancel)(pthread_t thread)
{
    return REAL_FUNC(pthread_cancel)(thread);
}

MAK_API MAK_PTHREAD_EXIT_ATTRIBUTE void WRAP_FUNC(pthread_exit)(void *retval)
{
    REAL_FUNC(pthread_exit)(retval);
}




            
#endif //MAK_THREADS
