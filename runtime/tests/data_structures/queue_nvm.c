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
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#define NUM_ITEMS 100000

// Atlas includes
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

// ID of Atlas persistent region
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

        fprintf(stderr, "Found queue at %p\n", (void *)Q);
        traverse();
    } else {
        node_t *node = (node_t *)nvm_alloc(sizeof(node_t), queue_rgn_id);
        node->val = -1; // dummy value
        node->next = NULL;
        Q = (queue_t *)nvm_alloc(sizeof(queue_t), queue_rgn_id);
        fprintf(stderr, "Created Q at %p\n", (void *)Q);

        Q->head_lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
        pthread_mutex_init(Q->head_lock, NULL);
        Q->tail_lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
        pthread_mutex_init(Q->tail_lock, NULL);

        NVM_BEGIN_DURABLE();

        Q->head = node;
        Q->tail = node;

        // Set the root of the Atlas persistent region
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
        // fprintf(stderr, "%p %d ", t, t->val);
        ++elem_count;
        t = t->next;
    }
    fprintf(stderr, "elem_count = %d\n", elem_count);
}

void enqueue(int val) {
    node_t *node = (node_t *)nvm_alloc(sizeof(node_t), queue_rgn_id);
    node->val = val;
    node->next = NULL;

    pthread_mutex_lock(Q->tail_lock);
    Q->tail->next = node;
#ifdef _FORCE_FAIL
    if (val == NUM_ITEMS / 2) exit(0);
#endif
    Q->tail = node;
    pthread_mutex_unlock(Q->tail_lock);
}

int dequeue(int *valp) {
    pthread_mutex_lock(Q->head_lock);
    node_t *node = Q->head;
    node_t *new_head = node->next;
    if (new_head == NULL) {
        pthread_mutex_unlock(Q->head_lock);
        return 0;
    }
    *valp = new_head->val;
#ifdef _FORCE_FAIL
    if (*valp == NUM_ITEMS / 4) exit(0);
#endif
    Q->head = new_head;
    pthread_mutex_unlock(Q->head_lock);

    nvm_free(node);
    return 1;
}

void *do_work() {
    pthread_mutex_lock(&ready_lock);
    ready = 1;
    pthread_mutex_unlock(&ready_lock);

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

        pthread_mutex_lock(&done_lock);
        t = done;
        pthread_mutex_unlock(&done_lock);
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

    // Initialize Atlas
    NVM_Initialize();
    // Create an Atlas persistent region
    queue_rgn_id = NVM_FindOrCreateRegion("queue", O_RDWR, NULL);
    // This contains the Atlas restart code to find any reusable data
    initialize();

    pthread_create(&thread, 0, (void *(*)(void *))do_work, 0);

    // wait for the child to be ready
    int t = 0;
    while (!t) {
        pthread_mutex_lock(&ready_lock);
        t = ready;
        pthread_mutex_unlock(&ready_lock);
    }

    for (int i = 0; i < NUM_ITEMS; ++i) {
        enqueue(i);
    }

    pthread_mutex_lock(&done_lock);
    done = 1;
    pthread_mutex_unlock(&done_lock);

    pthread_join(thread, NULL);

    // Close the Atlas persistent region
    NVM_CloseRegion(queue_rgn_id);
    // Optionally print Atlas stats
#ifdef NVM_STATS
    NVM_PrintStats();
#endif
    // Atlas bookkeeping
    NVM_Finalize();

    fprintf(stderr, "Total # items enqueued is %d\n", NUM_ITEMS);

    gettimeofday(&tv_end, NULL);
    fprintf(stderr, "time elapsed %ld us\n",
            tv_end.tv_usec - tv_start.tv_usec +
                (tv_end.tv_sec - tv_start.tv_sec) * 1000000);

    return 0;
}
