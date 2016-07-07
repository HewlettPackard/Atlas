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

#include <assert.h>
#include <stdio.h>
#include <sys/time.h>

// Atlas includes
#include "atlas_alloc.h"
#include "atlas_api.h"

#define LOOP_COUNT 1000000

void bar();

// ID of Atlas persistent region
uint32_t stores_rgn_id;

static inline uint64_t rdtsc() {
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return lo | ((uint64_t)hi << 32);
}

typedef int ARR_TYPE;

int main() {
    ARR_TYPE *arr;
    int count = 0;
    struct timeval tv_start;
    struct timeval tv_end;

#ifdef _FORCE_FAIL
    srand(time(NULL));
    int randval = rand() % LOOP_COUNT;
#endif

    // Initialize Atlas
    NVM_Initialize();
    // Create an Atlas persistent region
    stores_rgn_id = NVM_FindOrCreateRegion("stores", O_RDWR, NULL);
    // Allocate memory from the above persistent region
    arr = (ARR_TYPE *)nvm_alloc(LOOP_COUNT * sizeof(ARR_TYPE), stores_rgn_id);

    assert(!gettimeofday(&tv_start, NULL));

    uint64_t start = rdtsc();

    int i;

    // Atlas failure-atomic section
    for (i = 0; i < LOOP_COUNT; ++i) {
        NVM_BEGIN_DURABLE();

#ifdef _FORCE_FAIL
        if (i == randval) {
            exit(0);
        }
#endif
        arr[i] = i;

        NVM_END_DURABLE();
    }

    uint64_t end = rdtsc();

    assert(!gettimeofday(&tv_end, NULL));

    for (i = 0; i < LOOP_COUNT; ++i) {
        count += arr[i];
    }

    // Close the Atlas persistent region
    NVM_CloseRegion(stores_rgn_id);
    // Optionally print Atlas stats
#ifdef NVM_STATS
    NVM_PrintStats();
#endif
    // Atlas bookkeeping
    NVM_Finalize();

    fprintf(stderr, "Sum of elements is %d\n", count);
    fprintf(stderr, "time elapsed %ld us\n",
            tv_end.tv_usec - tv_start.tv_usec +
                (tv_end.tv_sec - tv_start.tv_sec) * 1000000);
    fprintf(stderr, "cycles: %ld\n", end - start);

    return 0;
}
