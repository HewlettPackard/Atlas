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

#include <alloca.h>
#include <assert.h>
#include <complex>
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
Node *InsertAfter = NULL;

uint32_t sll_rgn_id;

Node *createSingleNode(int Num, int NumBlanks) {
#if defined(_USE_MALLOC)
    Node *node = (Node *)malloc(sizeof(Node));
#else
    Node *node = (Node *)nvm_alloc(sizeof(Node), sll_rgn_id);
#endif
    int num_digits = log10(Num) + 1;
    int len = num_digits + NumBlanks;
#if defined(_USE_MALLOC)
    char *d = (char *)malloc((len + 1) * sizeof(char));
#else
    char *d = (char *)nvm_alloc((len + 1) * sizeof(char), sll_rgn_id);
#endif

    NVM_STR2(node->Data, d, sizeof(char *) * 8);

    char *tmp_s = (char *)alloca(
        (num_digits > NumBlanks ? num_digits + 1 : NumBlanks + 1) *
        sizeof(char));
    sprintf(tmp_s, "%d", Num);

    NVM_MEMCPY(node->Data, tmp_s, num_digits);

    for (int i = 0; i < NumBlanks; ++i) {
        tmp_s[i] = ' ';
    }
    NVM_MEMCPY(node->Data + num_digits, tmp_s, NumBlanks);
    NVM_STR2(node->Data[num_digits + NumBlanks], '\0', sizeof(char) * 8);
    return node;
}

Node *insertPass1(int Num, int NumBlanks, int NumFAI) {
    Node *node = createSingleNode(Num, NumBlanks);

    // In pass 1, the new node is the last node in the list
    NVM_STR2(node->Next, NULL, sizeof(node->Next) * 8);

    NVM_BEGIN_DURABLE();

    if (!SLL) {
        NVM_STR2(SLL, node, sizeof(SLL) * 8); // write-once
        InsertAfter = node;
    } else {
        NVM_STR2(InsertAfter->Next, node, sizeof(InsertAfter->Next) * 8);
        InsertAfter = node;
    }
    NVM_END_DURABLE();

    return node;
}

Node *insertPass2(int Num, int NumBlanks, int NumFAI) {
    Node **new_nodes = (Node **)malloc(NumFAI * sizeof(Node *));
    Node *list_iter = InsertAfter->Next;
    for (int i = 0; i < NumFAI; i++) {
        new_nodes[i] = createSingleNode(Num + i, NumBlanks);
        if (list_iter != NULL) {
            NVM_STR2(new_nodes[i]->Next, list_iter, sizeof(list_iter) * 8);
        } else {
            break;
        }
        list_iter = list_iter->Next;
    }

    NVM_BEGIN_DURABLE();

    for (int i = 0; i < NumFAI; i++) {
        NVM_STR2(InsertAfter->Next, new_nodes[i],
                 sizeof(InsertAfter->Next) * 8);
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
        //        fprintf(stderr, "%s ", tnode->Data);
        sum += atoi(tnode->Data);
        tnode = tnode->Next;
    }
    //    fprintf(stderr, "\n");
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

    NVM_Initialize();
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

    NVM_CloseRegion(sll_rgn_id);
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
