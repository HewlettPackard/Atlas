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

// Multithreaded version of sll
#include <alloca.h>
#include <assert.h>
#include <complex>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "atlas_alloc.h"
#include "atlas_api.h"

typedef struct Node {
    char *Data;
    struct Node *Next;
} Node;

typedef struct SearchResult {
    Node *Match;
    Node *Prev;
} SearchResult;

Node *SLL = NULL;
int NumItems = 0;
int NumFAI = 0;
int NumBlanks = 0;
pthread_mutex_t InsertPosLock = PTHREAD_MUTEX_INITIALIZER;
Node *InsertAfter = NULL;
int InsertValue;

Node *createSingleNode(int Num) {
    Node *node = (Node *)malloc(sizeof(Node));
    int num_digits = log10(Num) + 1;
    int len = num_digits + NumBlanks;
    node->Data = (char *)malloc((len + 1) * sizeof(char));
    char *tmp_s = (char *)alloca(
        (num_digits > NumBlanks ? num_digits + 1 : NumBlanks + 1) *
        sizeof(char));

    sprintf(tmp_s, "%d", Num);
    memcpy(node->Data, tmp_s, num_digits);
    node->Data[num_digits] = '\0';

    int i;
    for (i = 0; i < NumBlanks; ++i) {
        tmp_s[i] = ' ';
    }
    memcpy(node->Data + num_digits, tmp_s, NumBlanks);

    node->Data[len] = '\0';
    NVM_PSYNC(node->Data, len + 1);
    return node;
}

Node *copySingleNode(Node *orig) {
    Node *new_node = (Node *)malloc(sizeof(Node));
    new_node->Data = orig->Data;
    NVM_PSYNC(new_node->Data, 64 * 5);
    return new_node;
}

void flushNode(Node *node) {
    NVM_FLUSH(&node->Data);
    if (isOnDifferentCacheLine(&node->Data, &node->Next))
        NVM_FLUSH(&node->Next);
}

Node *insertPass1(int Num) {
    Node *node = createSingleNode(Num);

    // In pass 1, the new node is the last node in the list
    node->Next = NULL;

    flushNode(node);

    if (!SLL) {
        SLL = node; // write-once
        NVM_FLUSH(&SLL);
        InsertAfter = node;
    } else {
        InsertAfter->Next = node;
        NVM_FLUSH(&InsertAfter->Next);
        InsertAfter = node;
    }
    return node;
}

long printSLL() {
    long sum = 0;
    Node *tnode = SLL;
    while (tnode) {
        // fprintf(stderr, "%s ", tnode->Data);
        sum += atoi(tnode->Data);
        tnode = tnode->Next;
    }
    // fprintf(stderr, "\n");
    return sum;
}

void *insertPass2(__attribute__((unused)) void *threadid) {
    Node *insert_after;
    int insert_value;
    while (true) {
        pthread_mutex_lock(&InsertPosLock);

        insert_after = InsertAfter;
        insert_value = InsertValue;
        if (insert_value > NumItems) {
            pthread_mutex_unlock(&InsertPosLock);
            pthread_exit(NULL);
        }
        InsertValue += NumFAI;
        assert(InsertAfter);
        for (int i = 0; i < NumFAI && InsertAfter != NULL; ++i) {
            InsertAfter = InsertAfter->Next;
        }

        pthread_mutex_unlock(&InsertPosLock);

        Node *start_pos = insert_after;

        Node *first_inserted = createSingleNode(insert_value);
        Node *orig = start_pos->Next;
        Node *copy = NULL;
        int num_fai = NumFAI;
        if (num_fai == 1) {
            first_inserted->Next = orig;
        } else if (orig != NULL) {
            copy = copySingleNode(orig);
            first_inserted->Next = copy;
            orig = orig->Next;
        } else {
            first_inserted->Next = NULL;
            num_fai = 0;
        }

        flushNode(first_inserted);

        for (int i = 1; i < num_fai; i++) {
            Node *inserted = createSingleNode(insert_value + i);
            copy->Next = inserted;
            flushNode(copy);
            if (orig == NULL || i == num_fai - 1) {
                inserted->Next = orig;
                insert_after = orig;
            } else {
                copy = copySingleNode(orig);
                inserted->Next = copy;
                orig = orig->Next;
            }
            flushNode(inserted);
            if (insert_after == NULL) {
                break;
            }
        }
        start_pos->Next = first_inserted;
        NVM_FLUSH(&start_pos->Next);
    }
}

int main(int argc, char *argv[]) {
    struct timeval tv_start;
    struct timeval tv_end;
    gettimeofday(&tv_start, NULL);

    if (argc != 5) {
        fprintf(stderr,
                "usage: a.out numInts numBlanks numFAItems numThreads\n");
        exit(0);
    }

    NVM_Initialize();

    NumItems = atoi(argv[1]);
    NumBlanks = atoi(argv[2]);
    NumFAI = atoi(argv[3]);
    int T = atoi(argv[4]);
    fprintf(stderr, "N = %d K = %d X = %d T = %d\n", NumItems, NumBlanks,
            NumFAI, T);
    assert(!(NumItems % 2) && "N is not even");

    for (int i = 1; i < NumItems / 2 + 1; ++i) {
        insertPass1(i);
    }
    InsertAfter = SLL;
    InsertValue = NumItems / 2 + 1;

    long j;

    pthread_t *insert_threads = new pthread_t[T];
    int rc;

    for (j = 0; j < T; j++) {
        rc = pthread_create(&insert_threads[j], NULL, insertPass2, (void *)j);
        if (rc) {
            printf("ERROR: return code from pthread_create() is %d\n", rc);
            exit(-1);
        }
    }

    for (j = 0; j < T; j++) {
        pthread_join(insert_threads[j], NULL);
    }

    delete[] insert_threads;

    gettimeofday(&tv_end, NULL);

    fprintf(stderr, "Sum of elements is %ld\n", printSLL());

    fprintf(stderr, "time elapsed %ld us\n",
            tv_end.tv_usec - tv_start.tv_usec +
                (tv_end.tv_sec - tv_start.tv_sec) * 1000000);

    NVM_Finalize();

    return 0;
}
