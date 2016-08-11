#ifndef _MAHALU_THREAD_H
#define _MAKALU_THREAD_H

#ifdef MAK_THREADS

MAK_EXTERN pthread_mutex_t MAK_global_ml;
MAK_EXTERN pthread_mutex_t MAK_granule_ml[TINY_FREELISTS];

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


MAK_INNER void MAK_lock_gran(word gran);
MAK_INNER void MAK_unlock_gran(word gran);

MAK_INNER void MAK_thr_init(void);


#else // ! defined MAK_THREADS

#define MAK_LOCK()
#define MAK_UNLOCK()
#define MAK_LOCK_GRAN(gran) 
#define MAK_UNLOCK_GRAN(gran)

#define MAK_thr_init() 


#endif // MAK_THREADS
#endif  // _MAKALU_THREAD_H
