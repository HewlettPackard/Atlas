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

// Compile example: g++ sll.c
#include <alloca.h>
#include <assert.h>
#include <complex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

// Atlas includes
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
Node *InsertAfter = NULL;

#ifdef _FORCE_FAIL
int randval;
#endif

// ID of Atlas persistent region
uint32_t sll_rgn_id;

Node *createSingleNode(int Num, int NumBlanks) {
    Node *node = (Node *)nvm_alloc(sizeof(Node), sll_rgn_id);
    int num_digits = log10(Num) + 1;
    int len = num_digits + NumBlanks;
    char *d = (char *)nvm_alloc((len + 1) * sizeof(char), sll_rgn_id);
    node->Data = d;

    char *tmp_s = (char *)alloca(
        (num_digits > NumBlanks ? num_digits + 1 : NumBlanks + 1) *
        sizeof(char));
    sprintf(tmp_s, "%d", Num);

    memcpy(node->Data, tmp_s, num_digits);

    for (int i = 0; i < NumBlanks; ++i) {
        tmp_s[i] = ' ';
    }
    memcpy(node->Data + num_digits, tmp_s, NumBlanks);
    node->Data[num_digits + NumBlanks] = '\0';
    return node;
}

Node *insertPass1(int Num, int NumBlanks, __attribute__((unused)) int NumFAI) {
    Node *node = createSingleNode(Num, NumBlanks);

    // In pass 1, the new node is the last node in the list
    node->Next = NULL;

    NVM_BEGIN_DURABLE();

#ifdef _FORCE_FAIL
    if (Num == randval) {
        // printf ("num exiting is %i\n", Num);
        exit(0);
    }
#endif

    if (!SLL) {
        SLL = node; // write-once
        InsertAfter = node;
    } else {
        InsertAfter->Next = node;
        InsertAfter = node;
    }
    NVM_END_DURABLE();

    return node;
}

Node *insertPass2(int Num, int NumBlanks, int NumFAI) {
    Node **new_nodes = (Node **)malloc(NumFAI * sizeof(Node *));
    Node *list_iter = InsertAfter->Next;
    for (int i = 0; i < NumFAI; i++) {
#ifdef _FORCE_FAIL
        if (Num == randval) {
            // printf ("num exiting is %i\n", Num);
            exit(0);
        }
#endif
        new_nodes[i] = createSingleNode(Num + i, NumBlanks);
        if (list_iter != NULL) {
            new_nodes[i]->Next = list_iter;
        } else {
            break;
        }
        list_iter = list_iter->Next;
    }

    // Atlas failure-atomic region
    NVM_BEGIN_DURABLE();

    for (int i = 0; i < NumFAI; i++) {
        InsertAfter->Next = new_nodes[i];
        if (new_nodes[i]->Next != NULL) {
            InsertAfter = new_nodes[i]->Next;
        } else {
            InsertAfter = new_nodes[i];
            break;
        }
    }

    NVM_END_DURABLE();

    return InsertAfter;
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

int main(int argc, char *argv[]) {
    struct timeval tv_start;
    struct timeval tv_end;
    gettimeofday(&tv_start, NULL);

    if (argc != 4) {
        fprintf(stderr, "usage: a.out numInts numBlanks numFAItems\n");
        exit(0);
    }

    int N = atoi(argv[1]);
    int K = atoi(argv[2]);
    int X = atoi(argv[3]);
    fprintf(stderr, "N = %d K = %d X = %d\n", N, K, X);
    assert(!(N % 2) && "N is not even");

    assert(X > 0);
#ifdef _FORCE_FAIL
    srand(time(NULL));
    randval = rand() % N;
#endif

    // Initialize Atlas
    NVM_Initialize();
    // Create an Atlas persistent region
    sll_rgn_id = NVM_FindOrCreateRegion("sll_ll", O_RDWR, NULL);

    int i;
    for (i = 1; i < N / 2 + 1; ++i) {
        insertPass1(i, K, X);
    }
    InsertAfter = SLL;

    for (i = N / 2 + 1; i < N; i += X) {
        insertPass2(i, K, X);
    }
    int total_inserted = i - 1;
    insertPass2(i, K, N - total_inserted);

    fprintf(stderr, "Sum of elements is %ld\n", printSLL());

    // Close the Atlas persistent region
    NVM_CloseRegion(sll_rgn_id);
    // Optionally print Atlas stats
#ifdef NVM_STATS
    NVM_PrintStats();
#endif
    // Atlas bookkeeping
    NVM_Finalize();

    gettimeofday(&tv_end, NULL);

    fprintf(stderr, "time elapsed %ld us\n",
            tv_end.tv_usec - tv_start.tv_usec +
                (tv_end.tv_sec - tv_start.tv_sec) * 1000000);

    return 0;
}
