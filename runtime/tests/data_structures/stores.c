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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#define LOOP_COUNT 1000000

static inline uint64_t rdtsc() {
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return lo | ((uint64_t)hi << 32);
}

int main() {
    int *arr;
    int count = 0;
    struct timeval tv_start;
    struct timeval tv_end;

    arr = (int *)malloc(LOOP_COUNT * sizeof(int));

    assert(!gettimeofday(&tv_start, NULL));

    uint64_t start = rdtsc();

    int i;
    for (i = 0; i < LOOP_COUNT; ++i) {
        arr[i] = i;
    }

    uint64_t end = rdtsc();

    assert(!gettimeofday(&tv_end, NULL));

    for (i = 0; i < LOOP_COUNT; ++i) {
        count += arr[i];
    }
    fprintf(stderr, "Sum of elements is %d\n", count);
    fprintf(stderr, "time elapsed %ld us\n",
            tv_end.tv_usec - tv_start.tv_usec +
                (tv_end.tv_sec - tv_start.tv_sec) * 1000000);
    fprintf(stderr, "cycles: %ld\n", end - start);

    return 0;
}
