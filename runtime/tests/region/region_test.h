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

#include "atlas_alloc.h"
#include "atlas_api.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define THREADS 4
#define WORK 100
#define NUM_NODES 300

typedef struct node node;

struct node {
    node *next;
    int work;
};

node *root;
pthread_mutex_t root_lock;
int num_nodes;
uint32_t global_rgn_id;

void *add() {
    node *current;
    int i;

    pthread_mutex_lock(&root_lock);
    if (num_nodes >= NUM_NODES) {
        pthread_mutex_unlock(&root_lock);
        return 0;
    }
    i = num_nodes;
    pthread_mutex_unlock(&root_lock);

    while (i < NUM_NODES) {
        current = (node *)nvm_alloc(sizeof(node), global_rgn_id);
        current->work = (i * WORK) ^ i;

        pthread_mutex_lock(&root_lock);
        if (num_nodes >= NUM_NODES) {
            nvm_free(current);
            pthread_mutex_unlock(&root_lock);
            break;
        }
        i = ++num_nodes;
        printf("Adding node number %i\n", i);
        root->next = current;
        root = current;
        pthread_mutex_unlock(&root_lock);
    }
    return 0;
}

void dump() {
    while (root != NULL) {
        printf("Data is %i\n", root->work);
        root = root->next;
    }
}

void test(uint32_t rgn_id) {
    int i;
    pthread_attr_t attr;
    pthread_t *tid;
    global_rgn_id = rgn_id;

    root = (node *)NVM_GetRegionRoot(rgn_id);

    if (!root) {
        root = (node *)nvm_alloc(sizeof(node), rgn_id);
        root->next = NULL;
        root->work = WORK;
        num_nodes = 1;
        NVM_SetRegionRoot(rgn_id, root);
    }

    if (root->next == NULL) {
        assert(root && "Region root is NULL");

        pthread_mutex_init(&root_lock, NULL);
        tid = (pthread_t *)malloc(THREADS * sizeof(pthread_t));
        pthread_attr_init(&attr);
        pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

        for (i = 0; i < THREADS; i++) {
            printf("Creating thead %i\n", i);
            pthread_create(&tid[i], &attr, (void *(*)(void *))add, NULL);
            // pthread_create(&tid[i], &attr, *operation, NULL);
        }

        printf("Waiting for threads");

        for (i = 0; i < THREADS; i++) {
            pthread_join(tid[i], NULL);
        }

        free(tid);
    } else {
        dump();
    }

    sleep(2);
}
