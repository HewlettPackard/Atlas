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
 

#ifndef INTERNAL_API
#define INTERNAL_API

#ifdef __cplusplus
extern "C" {
#endif
    // TODO document these APIs.
    void nvm_acquire(void *lock_address);
    void nvm_rwlock_rdlock(void *lock_address);
    void nvm_rwlock_wrlock(void *lock_address);
    void nvm_release(void *lock_address);
    void nvm_rwlock_unlock(void *lock_address);
    void nvm_store(void *addr, size_t size);
    void nvm_log_alloc(void *addr);
    void nvm_log_free(void *addr);
    void nvm_memset(void *addr, size_t sz);
    void nvm_memcpy(void *dst, size_t sz);
    void nvm_memmove(void *dst, size_t sz);
    void nvm_strcpy(char *dst, size_t sz);
    void nvm_strcat(char *dst, size_t sz);
    size_t nvm_strlen(char *dst);//returns strlen + 1 - accounts for null char
    // TODO nvm_barrier should really be inlined. For that to happen,
    // the compiler must manually inline its instructions. Don't use this
    // interface within the library, instead use NVM_FLUSH
    void nvm_barrier(void*);

#if defined(_USE_TABLE_FLUSH)
    void AsyncDataFlush(void *p);
    void AsyncMemOpDataFlush(void *dst, size_t sz);
#endif

    
#ifdef __cplusplus
}
#endif    

// Read timestamp (x86)
static inline uint64_t atlas_rdtsc() {
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return lo | ((uint64_t)hi << 32);
}

#define NVM_LOG(var, size) {                                \
        nvm_store((void*)&(var), (size));                   \
    }                                                       \

#define NVM_LOCK(lock) {                                    \
        pthread_mutex_lock(&(lock));                        \
        nvm_acquire((void*)&(lock));                        \
    }                                                       \
        
#define NVM_UNLOCK(lock) {                                  \
        nvm_release((void*)&(lock));                        \
        pthread_mutex_unlock(&(lock));                      \
    }                                                       \
        
#define NVM_RWLOCK_RDLOCK(rwlock) {                         \
        pthread_rwlock_rdlock(&(rwlock));                   \
        nvm_rwlock_rdlock((void *)&(rwlock));               \
    }                                                       \
        
#define NVM_RWLOCK_WRLOCK(rwlock) {                         \
        pthread_rwlock_wrlock(&(rwlock));                   \
        nvm_rwlock_wrlock((void *)&(rwlock));               \
    }                                                       \
        
#define NVM_RWLOCK_UNLOCK(rwlock) {                         \
        nvm_rwlock_unlock((void *)&(rwlock));               \
        pthread_rwlock_unlock(&(rwlock));                   \
    }                                                       \
        
#if defined(_FLUSH_LOCAL_COMMIT) || defined(_FLUSH_GLOBAL_COMMIT)

#define NVM_STR(var,val,size) {                             \
        nvm_store((void*)&(var),((size)*8));                \
        var=val;                                            \
    }                                                       \
        
#define NVM_STR2(var,val,size) {                            \
        nvm_store((void*)&(var),(size));                    \
        var=val;                                            \
    }                                                       \

#define NVM_MEMSET(s,c,n) {                                 \
        nvm_memset((void *)(s), (size_t)(n));               \
        memset(s, c, n);                                    \
    }                                                       \
        
#define NVM_MEMCPY(dst, src, n) {                           \
        nvm_memcpy((void *)(dst), (size_t)(n));             \
        memcpy(dst, src, n);                                \
    }                                                       \

#define NVM_MEMMOVE(dst, src, n) {                          \
        nvm_memmove((void *)(dst), (size_t)(n));            \
        memmove(dst, src, n);                               \
    }                                                       \

#define NVM_STRCPY(dst, src) {                              \
        size_t sz=nvm_strlen(dst);                          \
        nvm_strcpy((void *)(dst),(size_t)(sz));             \
        strcpy(dst, src);                                   \
    }                                                       \

#define NVM_STRNCPY(dst, src, n) {                          \
        nvm_strcpy((void *)(dst),(size_t)(n));              \
        strncpy(dst, src, n);                               \
    }                                                       \

#define NVM_STRCAT(dst, src) {                              \
        size_t sz=nvm_strlen(dst);                          \
        nvm_strcat((void *)(dst),(size_t)(sz));             \
        strcat(dst, src);                                   \
    }                                                       \

#define NVM_STRNCAT(dst, src, n) {                          \
        size_t sz=nvm_strlen(dst);                          \
        nvm_strcat((void *)(dst),(size_t)(sz));             \
        strncat(dst, src, n);                               \
    }                                                       \

    
#elif defined(_DISABLE_DATA_FLUSH)

#define NVM_STR(var,val,size) {                             \
        nvm_store((void*)&(var),((size)*8));                \
        var=val;                                            \
    }                                                       \

#define NVM_STR2(var,val,size) {                            \
        nvm_store((void*)&(var),(size));                    \
        var=val;                                            \
    }                                                       \

#define NVM_MEMSET(s,c,n) {                                 \
        nvm_memset((void *)(s), (size_t)(n));               \
        memset(s, c, n);                                    \
    }                                                       \
        
#define NVM_MEMCPY(dst, src, n) {                           \
        nvm_memcpy((void *)(dst), (size_t)(n));             \
        memcpy(dst, src, n);                                \
    }                                                       \
        
#define NVM_MEMMOVE(dst, src, n) {                          \
        nvm_memmove((void *)(dst), (size_t)(n));            \
        memmove(dst, src, n);                               \
    }                                                       \

#define NVM_STRCPY(dst, src) {                              \
        size_t sz=nvm_strlen(dst);                          \
        nvm_strcpy((void *)(dst),(size_t)(sz));             \
        strcpy(dst, src);                                   \
    }                                                       \

#define NVM_STRNCPY(dst, src, n) {                          \
        nvm_strcpy((void *)(dst),(size_t)(n));              \
        strncpy(dst, src, n);                               \
    }                                                       \

#define NVM_STRCAT(dst, src) {                              \
        size_t sz=nvm_strlen(dst);                          \
        nvm_strcat((void *)(dst),(size_t)(sz));             \
        strcat(dst, src);                                   \
    }                                                       \

#define NVM_STRNCAT(dst, src, n) {                          \
        size_t sz=nvm_strlen(dst);                          \
        nvm_strcat((void *)(dst),(size_t)(sz));             \
        strncat(dst, src, n);                               \
    }                                                       \

#elif defined(_USE_TABLE_FLUSH)

#define NVM_STR(var,val,size) {                             \
        nvm_store((void*)&(var),((size)*8));                \
        var=val;                                            \
        AsyncDataFlush((void*)&(var));                      \
    }                                                       \

#define NVM_STR2(var,val,size) {                            \
        nvm_store((void*)&(var),(size));                    \
        var=val;                                            \
        AsyncDataFlush((void*)&(var));                      \
    }                                                       \

#define NVM_MEMSET(s,c,n) {                                 \
        nvm_memset((void *)(s), (size_t)(n));               \
        memset(s, c, n);                                    \
        AsyncMemOpDataFlush((void*)(s), n);                 \
    }                                                       \
        
#define NVM_MEMCPY(dst, src, n) {                           \
        nvm_memcpy((void *)(dst), (size_t)(n));             \
        memcpy(dst, src, n);                                \
        AsyncMemOpDataFlush((void*)(dst), n);               \
    }                                                       \
        
#define NVM_MEMMOVE(dst, src, n) {                          \
        nvm_memmove((void *)(dst), (size_t)(n));            \
        memmove(dst, src, n);                               \
        AsyncMemOpDataFlush((void*)(dst), n);               \
    }                                                       \

#define NVM_STRCPY(dst, src) {                              \
        size_t sz=nvm_strlen(dst);                          \
        nvm_strcpy((dst),(size_t)(sz));                     \
        strcpy(dst, src);                                   \
        AsyncMemOpDataFlush((void*)(dst), sz);              \
    }                                                       \

#define NVM_STRNCPY(dst, src, n) {                          \
        nvm_strcpy((dst),(size_t)(n));                      \
        strncpy(dst, src, n);                               \
        AsyncMemOpDataFlush((void*)(dst), n);               \
    }                                                       \

#define NVM_STRCAT(dst, src) {                              \
        size_t sz=nvm_strlen(dst);                          \
        nvm_strcat((dst),(size_t)(sz));                     \
        strcat(dst, src);                                   \
        AsyncMemOpDataFlush((void*)(dst), sz);              \
    }                                                       \

#define NVM_STRNCAT(dst, src, n) {                          \
        size_t sz=nvm_strlen(dst);                          \
        nvm_strcat((dst),(size_t)(sz));                     \
        strncat(dst, src, n);                               \
        AsyncMemOpDataFlush((void*)(dst), sz);              \
    }                                                       \

#else

// TODO for transient locations, a filter avoids logging and flushing.
// Currently, this filter is being called twice. This should be optimized
// to call once only. Ensure that the fix is done in the compiled code-path
// as well, i.e. it should be there for nvm_barrier as well.

#define NVM_STR(var,val,size) {                             \
        nvm_store((void*)&(var),((size)*8));                \
        var=val;                                            \
        NVM_FLUSH_ACQ_COND(&var);                           \
    }                                                       \

#define NVM_STR2(var,val,size) {                            \
        nvm_store((void*)&(var),(size));                    \
        var=val;                                            \
        NVM_FLUSH_ACQ_COND(&var);                           \
    }                                                       \

#define NVM_MEMSET(s,c,n) {                                 \
        nvm_memset((void *)(s), (size_t)(n));               \
        memset(s, c, n);                                    \
        NVM_PSYNC_ACQ_COND(s, n);                           \
    }                                                       \
        
#define NVM_MEMCPY(dst, src, n) {                           \
        nvm_memcpy((void *)(dst), (size_t)(n));             \
        memcpy(dst, src, n);                                \
        NVM_PSYNC_ACQ_COND(dst, n);                         \
    }                                                       \
        
#define NVM_MEMMOVE(dst, src, n) {                          \
        nvm_memmove((void *)(dst), (size_t)(n));            \
        memmove(dst, src, n);                               \
        NVM_PSYNC_ACQ_COND(dst, n);                         \
    }                                                       \

#define NVM_STRCPY(dst, src) {                              \
        size_t sz=nvm_strlen(dst);                          \
        nvm_strcpy((void *)(dst),(size_t)(sz));             \
        strcpy(dst, src);                                   \
        NVM_PSYNC_ACQ_COND(dst, sz);                        \
    }                                                       \

#define NVM_STRNCPY(dst, src, n) {                          \
        nvm_strcpy((void *)(dst),(size_t)(n));              \
        strncpy(dst, src, n);                               \
        NVM_PSYNC_ACQ_COND(dst, n);                         \
    }                                                       \

#define NVM_STRCAT(dst, src) {                              \
        size_t sz=nvm_strlen(dst);          \
        nvm_strcat((void *)(dst),(size_t)(sz));             \
        strcat(dst, src);                                   \
        NVM_PSYNC_ACQ_COND(dst, sz);                        \
    }                                                       \

#define NVM_STRNCAT(dst, src, n) {                          \
        size_t sz=nvm_strlen(dst);                        \
        nvm_strcat((void *)(dst),(size_t)(sz));             \
        strncat(dst, src, n);                               \
        NVM_PSYNC_ACQ_COND(dst, sz);                        \
    }                                                       \

#endif 

#endif
