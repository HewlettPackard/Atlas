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

// This is an example of a program that has been manually instrumented
// with Atlas internal APIs. This is just for understanding
// purposes. It is recommended that users use an Atlas-aware compiler
// (such as the one provided through the LLVM compiler-plugin under
// the root Atlas directory) to perform this instrumentation. Manual
// instrumentation is burdensome and error-prone and probably has very
// little performance advantage. If you still want to instrument
// manually, make sure you compile the manually instrumented program
// using your favorite compiler, not the Atlas-aware compiler.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "atlas_alloc.h"
#include "atlas_api.h"

#define LOOP_COUNT 1000000

void bar();

uint32_t stores_rgn_id;

inline uint64_t rdtsc() {
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return lo | ((uint64_t)hi << 32);
}

typedef int ARR_TYPE;

int main(int argc, char *argv[]) {
    ARR_TYPE *arr;
    int count = 0;
    struct timeval tv_start;
    struct timeval tv_end;

    NVM_Initialize();
    stores_rgn_id = NVM_FindOrCreateRegion("stores", O_RDWR, NULL);
    arr = (ARR_TYPE *)nvm_alloc(LOOP_COUNT * sizeof(ARR_TYPE), stores_rgn_id);

    assert(!gettimeofday(&tv_start, NULL));

    uint64_t start = rdtsc();

    int i;

    //    NVM_BEGIN_DURABLE();
    for (i = 0; i < LOOP_COUNT; i++) {
        NVM_BEGIN_DURABLE();

        NVM_STR2(arr[i], i, sizeof(arr[i]) * 8);

        NVM_END_DURABLE();
    }
    //    NVM_END_DURABLE();

    assert(!gettimeofday(&tv_end, NULL));

    uint64_t end = rdtsc();

    for (i = 0; i < LOOP_COUNT; ++i) {
        count += arr[i];
    }

    NVM_CloseRegion(stores_rgn_id);

#ifdef NVM_STATS
    NVM_PrintStats();
#endif

    NVM_Finalize();

    fprintf(stderr, "Sume of elements is %d\n", count);
    fprintf(stderr, "time elapsed %ld us\n",
            tv_end.tv_usec - tv_start.tv_usec +
                (tv_end.tv_sec - tv_start.tv_sec) * 1000000);
    fprintf(stderr, "cycles: %ld\n", end - start);

    return 0;
}
