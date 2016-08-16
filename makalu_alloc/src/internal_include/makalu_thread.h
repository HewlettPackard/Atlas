#ifndef _MAHALU_THREAD_H
#define _MAKALU_THREAD_H

#ifdef MAK_THREADS

#include <pthread.h>

MAK_EXTERN long MAK_n_markers;
MAK_EXTERN MAK_bool MAK_parallel_mark;

#define MAK_LOCK() { pthread_mutex_lock(&MAK_global_ml); }
#define MAK_UNLOCK() { pthread_mutex_unlock(&MAK_global_ml); }

#define MAK_LOCK_GRAN(gran) \
        { \
                MAK_lock_gran(gran); \
        }

#define MAK_UNLOCK_GRAN(gran) \
        { \
                MAK_unlock_gran(gran); \
        }
# define DISABLE_CANCEL(state) \
        { pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &state); }


MAK_INNER void MAK_lock_gran(word gran);
MAK_INNER void MAK_unlock_gran(word gran);

MAK_INNER void MAK_acquire_mark_lock(void);
MAK_INNER void MAK_release_mark_lock(void);
MAK_INNER void MAK_wait_marker(void);
MAK_INNER void MAK_notify_all_marker(void);

MAK_INNER void MAK_thr_init(void);
MAK_INNER void MAK_start_mark_threads(void);

#define WRAP_FUNC(f) GC_##f
#define REAL_FUNC(f) f

#else // ! defined MAK_THREADS

#define MAK_LOCK()
#define MAK_UNLOCK()
#define MAK_LOCK_GRAN(gran) 
#define MAK_UNLOCK_GRAN(gran)

#define MAK_acquire_mark_lock()
#define MAK_release_mark_lock()
#define MAK_wait_marker()
#define MAK_notify_all_marker()


# define DISABLE_CANCEL(state) 

#define MAK_thr_init() 
#define MAK_start_mark_threads()

#endif // MAK_THREADS
#endif  // _MAKALU_THREAD_H
