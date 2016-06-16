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


#ifndef ATLAS_API_H
#define ATLAS_API_H

#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// Here are the APIs for initializing/finalizing Atlas. Each of the
// following 2 interfaces must be called only once.

///
/// Initialize Atlas internal data structures. This should be
/// called before persistent memory access.
///
void NVM_Initialize();

///
/// Finalize Atlas internal data structures. This should be called
/// before normal program exit. If not called, the implementation
/// will assume that program exit was abnormal and will require
/// invocation of recovery before restart.
///
void NVM_Finalize();

//
// No special interfaces are required for lock-based critical
// sections if compiler support is available. Use the
// compiler-plugin to take advantage of automatic instrumentation
// of critical sections.
//

///
/// The following 2 interfaces demarcate a failure-atomic section
/// of code, i.e. code where persistent locations are updated and
/// all-or-nothing behavior of those updates is required. Note that
/// no isolation among threads is provided by these 2 interfaces.
///
void nvm_begin_durable();
void nvm_end_durable();

//
// The following interfaces are for low-level programming of
// persistent memory, where the high-level consistency support
// afforded by Atlas is not used. Instead, persistence is explicitly
// managed by the following interfaces.
//

///
/// Is the following address with associated size within an open
/// persistent region?
///
int NVM_IsInOpenPR(void *addr, size_t sz /* in bytes */);

///
/// Persistent sync of a range of addresses
///
void nvm_psync(void *addr, size_t sz /* in bytes */);

///
/// Persistent sync of a range of addresses without a trailing barrier
///
void nvm_psync_acq(void *addr, size_t sz /* in bytes */);

// This may be invoked by a user program to print out Atlas statistics
#ifdef NVM_STATS
    void NVM_PrintStats();
#endif

#ifdef __cplusplus
}
#endif

// End of Atlas APIs

#ifdef NVM_STATS
extern __thread uint64_t num_flushes;
#endif

// Useful macros
#define NVM_BEGIN_DURABLE() nvm_begin_durable()
#define NVM_END_DURABLE() nvm_end_durable()

#define NVM_CLFLUSH(p) nvm_clflush((char*)(void*)(p))

#ifndef DISABLE_FLUSHES
#define NVM_FLUSH(p)                                        \
    {   full_fence();                                       \
        NVM_CLFLUSH((p));                                   \
        full_fence();                                       \
    }

#define NVM_FLUSH_COND(p)                                   \
    { if (NVM_IsInOpenPR(p, 1)) {                           \
            full_fence();                                   \
            NVM_CLFLUSH((p));                               \
            full_fence();                                   \
        }                                                   \
    }

#define NVM_FLUSH_ACQ(p)                                    \
    {   full_fence();                                       \
        NVM_CLFLUSH(p);                                     \
    }

#define NVM_FLUSH_ACQ_COND(p)                               \
    { if (NVM_IsInOpenPR(p, 1)) {                           \
            full_fence();                                   \
            NVM_CLFLUSH(p);                                 \
        }                                                   \
    }

#define NVM_PSYNC(p1,s) nvm_psync(p1,s)

#define NVM_PSYNC_COND(p1,s)                                \
    { if (NVM_IsInOpenPR(p1, s)) nvm_psync(p1,s); }

#define NVM_PSYNC_ACQ(p1,s)                                 \
    {                                                       \
        nvm_psync_acq(p1,s);                                \
    }                                                       \

#define NVM_PSYNC_ACQ_COND(p1,s)                            \
    {                                                       \
        if (NVM_IsInOpenPR(p1, s)) nvm_psync_acq(p1, s);    \
    }                                                       \

#else
#define NVM_FLUSH(p)
#define NVM_FLUSH_COND(p)
#define NVM_FLUSH_ACQ(p)
#define NVM_FLUSH_ACQ_COND(p)
#define NVM_PSYNC(p1,s)
#define NVM_PSYNC_COND(p1,s)
#define NVM_PSYNC_ACQ(p1,s)
#define NVM_PSYNC_ACQ_COND(p1,s)
#endif

static __inline void nvm_clflush(const void *p)
{
#ifndef DISABLE_FLUSHES
#ifdef NVM_STATS
    ++num_flushes;
#endif
    __asm__ __volatile__ (
        "clflush %0 \n" : "+m" (*(char*)(p))
        );
#endif
}

// Used in conjunction with clflush.
static __inline void full_fence() {
    __asm__ __volatile__ ("mfence" ::: "memory");
  }

#endif
