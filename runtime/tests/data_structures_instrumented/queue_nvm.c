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

// Adapted from Michael & Scott, "Simple, Fast, and Practical Non-Blocking
// and Blocking Concurrent Queue Algorithms", PODC 1996.

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
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#define NUM_ITEMS 100000

#include "atlas_alloc.h"
#include "atlas_api.h"

typedef struct node_t {
    int val;
    struct node_t *next;
} node_t;

typedef struct queue_t {
    node_t *head;
    node_t *tail;
    pthread_mutex_t *head_lock;
    pthread_mutex_t *tail_lock;
} queue_t;

queue_t *Q;

int ready = 0;
int done = 0;

pthread_mutex_t ready_lock;
pthread_mutex_t done_lock;

uint32_t queue_rgn_id;

void traverse();

void initialize() {
    void *rgn_root = NVM_GetRegionRoot(queue_rgn_id);
    if (rgn_root) {
        Q = (queue_t *)rgn_root;
        Q->head_lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
        pthread_mutex_init(Q->head_lock, NULL);
        Q->tail_lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
        pthread_mutex_init(Q->tail_lock, NULL);

        fprintf(stderr, "Found queue at %p\n", Q);
        traverse();
    } else {
        node_t *node =
#if defined(_USE_MALLOC)
            (node_t *)malloc(sizeof(node_t));
#else
            (node_t *)nvm_alloc(sizeof(node_t), queue_rgn_id);
#endif
        assert(node);

        NVM_STR2(node->val, -1, sizeof(int) * 8); // dummy value
        NVM_STR2(node->next, NULL, sizeof(node_t *) * 8);

#if defined(_USE_MALLOC)
        Q = (queue_t *)malloc(sizeof(queue_t));
#else
        Q = (queue_t *)nvm_alloc(sizeof(queue_t), queue_rgn_id);
#endif
        assert(Q);
        fprintf(stderr, "Created Q at %p\n", Q);

        Q->head_lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
        pthread_mutex_init(Q->head_lock, NULL);
        Q->tail_lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
        pthread_mutex_init(Q->tail_lock, NULL);

        NVM_BEGIN_DURABLE();

        NVM_STR2(Q->head, node, sizeof(node_t *) * 8);
        NVM_STR2(Q->tail, node, sizeof(node_t *) * 8);

        NVM_SetRegionRoot(queue_rgn_id, Q);

        NVM_END_DURABLE();
    }
}

// not thread safe
void traverse() {
    assert(Q);
    assert(Q->head);
    assert(Q->tail);

    node_t *t = Q->head;
    fprintf(stderr, "Contents of existing queue: ");
    int elem_count = 0;
    while (t) {
        //        fprintf(stderr, "%p %d ", t, t->val);
        ++elem_count;
        t = t->next;
    }
    fprintf(stderr, "elem_count = %d\n", elem_count);
}

void enqueue(int val) {
    node_t *node =
#if defined(_USE_MALLOC)
        (node_t *)malloc(sizeof(node_t));
#else
        (node_t *)nvm_alloc(sizeof(node_t), queue_rgn_id);
#endif

    assert(node);

    NVM_STR2(node->val, val, sizeof(int) * 8);
    NVM_STR2(node->next, NULL, sizeof(node_t *) * 8);

    NVM_LOCK(*(Q->tail_lock));
    NVM_STR2(Q->tail->next, node, sizeof(node_t *) * 8);
#ifdef _FORCE_FAIL
    if (val == NUM_ITEMS / 2)
        exit(0);
#endif
    NVM_STR2(Q->tail, node, sizeof(node_t *) * 8);
    NVM_UNLOCK(*(Q->tail_lock));
}

int dequeue(int *valp) {
    NVM_LOCK(*(Q->head_lock));
    // note that not everything of type node_t is persistent
    // In the following statement, we have a transient pointer to
    // persistent data which is just fine. If there is a crash, the
    // transient pointer goes away.
    node_t *node = Q->head;
    node_t *new_head = node->next;
    if (new_head == NULL) {
        NVM_UNLOCK(*(Q->head_lock));
        return 0;
    }
    *valp = new_head->val;
#ifdef _FORCE_FAIL
    if (*valp == NUM_ITEMS / 4)
        exit(0);
#endif
    NVM_STR2(Q->head, new_head, sizeof(node_t *) * 8);
    NVM_UNLOCK(*(Q->head_lock));

#if defined(_USE_MALLOC)
    free(node);
#else
    nvm_free(node);
#endif
    return 1;
}

void *do_work() {
    // TODO All lock/unlock must be instrumented, regardless of whether the
    // critical sections have accesses to persistent locations
    NVM_LOCK(ready_lock);
    ready = 1;
    NVM_UNLOCK(ready_lock);

    int global_count = 0;
    int t = 0;
    while (1) {
        int val;
        int status = dequeue(&val);
        if (status) {
            ++global_count;
        } else if (t) {
            break;
        }

        NVM_LOCK(done_lock);
        t = done;
        NVM_UNLOCK(done_lock);
    }
    fprintf(stderr, "Total # items dequeued is %d\n", global_count);

#ifdef NVM_STATS
    NVM_PrintStats();
#endif

    return 0;
}

int main() {
    pthread_t thread;
    struct timeval tv_start;
    struct timeval tv_end;

    gettimeofday(&tv_start, NULL);

    NVM_Initialize();
    queue_rgn_id = NVM_FindOrCreateRegion("queue", O_RDWR, NULL);

    initialize();

    pthread_create(&thread, 0, (void *(*)(void *))do_work, 0);

    // wait for the child to be ready
    int t = 0;
    while (!t) {
        NVM_LOCK(ready_lock);
        t = ready;
        NVM_UNLOCK(ready_lock);
    }

    int i;
    for (i = 0; i < NUM_ITEMS; ++i) {
        enqueue(i);
    }

    NVM_LOCK(done_lock);
    done = 1;
    NVM_UNLOCK(done_lock);

    pthread_join(thread, NULL);

    NVM_CloseRegion(queue_rgn_id);

#ifdef NVM_STATS
    NVM_PrintStats();
#endif

    NVM_Finalize();

    fprintf(stderr, "Total # items enqueued is %d\n", NUM_ITEMS);

    gettimeofday(&tv_end, NULL);
    fprintf(stderr, "time elapsed %ld us\n",
            tv_end.tv_usec - tv_start.tv_usec +
                (tv_end.tv_sec - tv_start.tv_sec) * 1000000);

    return 0;
}
