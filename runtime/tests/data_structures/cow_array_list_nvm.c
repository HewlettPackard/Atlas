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
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "atlas_alloc.h"
#include "atlas_api.h"

#define INITIAL_SIZE 1000
#define NUM_ITER 10

typedef struct COW_AL_INTERNAL {
    int *data;
    int size;
    int version;
} COW_AL_INTERNAL;

typedef struct COW_AL {
    COW_AL_INTERNAL *cal_int;
} COW_AL;

typedef struct COW_AL_ITER {
    COW_AL_INTERNAL *cal_int;
} COW_AL_ITER;

COW_AL *create_cal();
void add_cal(COW_AL *cal, int num);
void insert_cal(COW_AL *cal, int index, int num);
int remove_cal(COW_AL *cal, int index);
int cal_contains(COW_AL *cal, int num);
int cal_get(COW_AL *cal, int index);
int size(COW_AL *cal);
COW_AL_ITER *create_iter(COW_AL *);
int *iter_first(COW_AL_ITER *);
int *iter_last(COW_AL_ITER *);
void print_cal(COW_AL *);

pthread_mutex_t lock_cal;
pthread_mutex_t ready_lock;

int ready = 0;
int done = 0;

COW_AL *alist = 0;

uint32_t cal_rgn_id;

COW_AL_INTERNAL *GetNewCalInternal() {
    COW_AL_INTERNAL *cal_int =
        (COW_AL_INTERNAL *)nvm_alloc(sizeof(COW_AL_INTERNAL), cal_rgn_id);
    assert(cal_int);
    return cal_int;
}

int DoAtomicSwitch(COW_AL *cal, COW_AL_INTERNAL *cal_int) {
    pthread_mutex_lock(&lock_cal);
    if (cal->cal_int->version != cal_int->version) {
        pthread_mutex_unlock(&lock_cal);
        return 0;
    }
    cal_int->version = cal_int->version + 1;
    cal->cal_int = cal_int;
    pthread_mutex_unlock(&lock_cal);
    return 1;
}

COW_AL *create_cal() {
    COW_AL_INTERNAL *cal_int = GetNewCalInternal();

    int *tmp = (int *)nvm_alloc(INITIAL_SIZE * sizeof(int), cal_rgn_id);
    assert(tmp);

    for (int i = 0; i < INITIAL_SIZE; ++i) {
        tmp[i] = i;
    }

    cal_int->data = tmp;
    cal_int->size = INITIAL_SIZE;
    cal_int->version = 0;

    COW_AL *cal = (COW_AL *)nvm_alloc(sizeof(COW_AL), cal_rgn_id);
    assert(cal);
    cal->cal_int = cal_int;

    return cal;
}

void add_cal(COW_AL *cal, int num) {
    int status = 0;
    int iter = 0;
    do {
        COW_AL_INTERNAL *cal_int = GetNewCalInternal();
        cal_int->version = cal->cal_int->version;
        cal_int->size = cal->cal_int->size + 1;
        int *tmp = (int *)nvm_alloc(cal_int->size * sizeof(int), cal_rgn_id);
        assert(tmp);
        cal_int->data = tmp;
        for (int i = 0; i < cal_int->size - 1; ++i) {
            cal_int->data[i] = cal->cal_int->data[i];
        }
        cal_int->data[cal_int->size - 1] = num;
        status = DoAtomicSwitch(cal, cal_int);
        ++iter;
    } while (!status);
    // fprintf(stderr, "Added in %d attempts\n", iter);
}

void insert_cal(COW_AL *cal, int index, int num) {
    int status = 0;
    int iter = 0;
    do {
        COW_AL_INTERNAL *cal_int = GetNewCalInternal();
        cal_int->version = cal->cal_int->version;
        cal_int->size = cal->cal_int->size + 1;
        assert(index < cal_int->size);
        int *tmp = (int *)nvm_alloc(cal_int->size * sizeof(int), cal_rgn_id);
        assert(tmp);
        cal_int->data = tmp;
        int i;
        for (i = 0; i < index; ++i) {
            cal_int->data[i] = cal->cal_int->data[i];
        }
        cal_int->data[index] = num;
        for (i = index + 1; i < cal_int->size; ++i) {
            cal_int->data[i] = cal->cal_int->data[i - 1];
        }
        status = DoAtomicSwitch(cal, cal_int);
        ++iter;
    } while (!status);
    // fprintf(stderr, "Inserted in %d attempts\n", iter);
}

int remove_cal(COW_AL *cal, int index) {
    int status = 0;
    int iter = 0;
    int removed_item;
    do {
        COW_AL_INTERNAL *cal_int = GetNewCalInternal();
        assert(index < cal->cal_int->size);
        cal_int->version = cal->cal_int->version;
        cal_int->size = cal->cal_int->size - 1;
        int *tmp = (int *)nvm_alloc(cal_int->size * sizeof(int), cal_rgn_id);
        assert(tmp);
        cal_int->data = tmp;
        int i;
        for (i = 0; i < index; ++i) {
            cal_int->data[i] = cal->cal_int->data[i];
        }
        removed_item = cal->cal_int->data[index];
        for (i = index + 1; i < cal_int->size + 1; ++i) {
            cal_int->data[i - 1] = cal->cal_int->data[i];
        }
        status = DoAtomicSwitch(cal, cal_int);
        ++iter;
    } while (!status);
    // fprintf(stderr, "Removed in %d attempts\n", iter);
    return removed_item;
}

int cal_contains(COW_AL *cal, int num) {
    int *data = cal->cal_int->data;
    for (int i = 0; i < cal->cal_int->size; ++i) {
        if (data[i] == num) {
            return 1;
        }
    }
    return 0;
}

int cal_get(COW_AL *cal, int index) {
    assert(index < cal->cal_int->size);
    return cal->cal_int->data[index];
}

int size(COW_AL *cal) {
    return cal->cal_int->size;
}

COW_AL_ITER *create_iter(COW_AL *al) {
    COW_AL_ITER *cal_iter = (COW_AL_ITER *)malloc(sizeof(COW_AL_ITER));
    assert(cal_iter);

    cal_iter->cal_int = al->cal_int;
    return cal_iter;
}

int *iter_first(COW_AL_ITER *it) {
    return it->cal_int->data;
}

int *iter_last(COW_AL_ITER *it) {
    COW_AL_INTERNAL *cal_int = it->cal_int;
    return cal_int->data + (cal_int->size - 1);
}

void print_cal(COW_AL *cal) {
    COW_AL_ITER *alist_iter = create_iter(cal);
    int *ifirst = iter_first(alist_iter);
    int *ilast = iter_last(alist_iter);
    while (ifirst) {
        fprintf(stderr, "%d ", *ifirst);
        if (ifirst == ilast) {
            break;
        }
        ++ifirst;
    }
    fprintf(stderr, "\n");
    free(alist_iter);
}

uint64_t sum_cal(COW_AL *cal) {
    uint64_t sum = 0;
    COW_AL_ITER *alist_iter = create_iter(cal);
    int *ifirst = iter_first(alist_iter);
    int *ilast = iter_last(alist_iter);
    while (ifirst) {
        sum += *ifirst;
        if (ifirst == ilast) {
            break;
        }
        ++ifirst;
    }
    free(alist_iter);
    // fprintf(stderr, "%d\n", sum);
    return sum;
}

void *do_work() {
    pthread_mutex_lock(&ready_lock);
    ready = 1;
    pthread_mutex_unlock(&ready_lock);

    uint64_t sum = 0;
    int count = 0;
    while (count < NUM_ITER) {
        remove_cal(alist, 85);
        sum += sum_cal(alist);

        insert_cal(alist, 205, 210);
        assert(cal_contains(alist, 210));
        sum += sum_cal(alist);

#ifdef _FORCE_FAIL
        if (count == NUM_ITER/2) exit(0);
#endif

        add_cal(alist, 105);
        sum += sum_cal(alist);

        ++count;
    }
    fprintf(stderr, "child sum is %ld\n", sum);
    return 0;
}

int main() {
    struct timeval tv_start;
    struct timeval tv_end;
    pthread_t th;

    gettimeofday(&tv_start, NULL);

    NVM_Initialize();
    cal_rgn_id = NVM_FindOrCreateRegion("cal", O_RDWR, NULL);

    alist = create_cal();

    NVM_SetRegionRoot(cal_rgn_id, alist);

    pthread_create(&th, 0, (void *(*)(void *))do_work, 0);

    int t = 0;
    while (!t) {
        pthread_mutex_lock(&ready_lock);
        t = ready;
        pthread_mutex_unlock(&ready_lock);
    }

    uint64_t sum = 0;
    int count = 0;
    while (count < NUM_ITER) {
        sum += sum_cal(alist);

        add_cal(alist, 100);
        sum += sum_cal(alist);

        insert_cal(alist, 200, 210);
        sum += sum_cal(alist);

#ifdef _FORCE_FAIL
        if (count == NUM_ITER/2) exit(0);
#endif
        remove_cal(alist, 50);
        sum += sum_cal(alist);

        ++count;
    }

    fprintf(stderr, "parent sum is %ld\n", sum);

    pthread_join(th, NULL);

    NVM_CloseRegion(cal_rgn_id);
#ifdef NVM_STATS
    NVM_PrintStats();
#endif
    NVM_Finalize();

    gettimeofday(&tv_end, NULL);
    fprintf(stderr, "time elapsed %ld us\n",
            tv_end.tv_usec - tv_start.tv_usec +
                (tv_end.tv_sec - tv_start.tv_sec) * 1000000);

    return 0;
}

/*
int main(int argc, char *argv[])
{
    COW_AL * alist = create_cal();
    sum_cal(*alist);
    add_cal(alist, 100);
    sum_cal(*alist);
    insert_cal(alist, 2, 210);
    sum_cal(*alist);
    remove_cal(alist, 10);
    sum_cal(*alist);
    fprintf(stderr, "Does 23 exist? %s\n",
            cal_contains(alist, 23) ? "yes" : "no");
    fprintf(stderr, "Elements at positions 5 and 86 are %d and %d\n",
            cal_get(alist, 5), cal_get(alist, 86));
}

*/
