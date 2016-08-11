#ifdef MAK_THREADS


#include "makalu_internal.h"

MAK_INNER pthread_mutex_t MAK_global_ml = PTHREAD_MUTEX_INITIALIZER;
MAK_INNER pthread_mutex_t MAK_granule_ml[TINY_FREELISTS];

MAK_INNER void MAK_lock_gran(word gran)
{
    int g = gran >= TINY_FREELISTS ? 0 : gran;
    pthread_mutex_lock(&(MAK_granule_ml[g]));
}

MAK_INNER void MAK_unlock_gran(word gran){
      int g = gran >= TINY_FREELISTS ? 0 : gran;
      pthread_mutex_unlock(&(MAK_granule_ml[g]));
}


MAK_INNER void MAK_thr_init(void)
{
    int i;
    for (i=0; i < TINY_FREELISTS; i++)
    {
        pthread_mutex_init(MAK_granule_ml + i, NULL);
    }
}


#endif //MAK_THREADS
