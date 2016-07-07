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

Node *createSingleNode(int Num, int NumBlanks) {
    Node *node = (Node *)malloc(sizeof(Node));
    int num_digits = log10(Num) + 1;
    int len = num_digits + NumBlanks;
    node->Data = (char *)malloc((len + 1) * sizeof(char));

    char *tmp_s = (char *)alloca(
        (num_digits > NumBlanks ? num_digits + 1 : NumBlanks + 1) *
        sizeof(char));
    sprintf(tmp_s, "%d", Num);

    memcpy(node->Data, tmp_s, num_digits);

    for (int i = 0; i < NumBlanks; ++i) {
        tmp_s[i] = ' ';
    }
    memcpy(node->Data + num_digits, tmp_s, NumBlanks);
    node->Data[len] = '\0';

    // Atlas low-level data sync interface to make sure the entire
    // node reaches persistent memory
    NVM_PSYNC(node->Data, len + 1);
    return node;
}

Node *copySingleNode(Node *orig) {
    Node *new_node = (Node *)malloc(sizeof(Node));
    new_node->Data = orig->Data;

    // Atlas low-level data sync interface
    NVM_PSYNC(new_node->Data, 64 * 1);
    return new_node;
}

void flushNode(Node *node) {
    // Atlas low-level interface to ensure the dirty cache line
    // reaches persistent memory before proceeding
    NVM_FLUSH(&node->Data);
    // Filter out same cache line
    if (isOnDifferentCacheLine(&node->Data, &node->Next))
        NVM_FLUSH(&node->Next);
}

Node *insertPass1(int Num, int NumBlanks) {
    Node *node = createSingleNode(Num, NumBlanks);

    // In pass 1, the new node is the last node in the list
    node->Next = NULL;

    flushNode(node);

    if (!SLL) {
        SLL = node; // write-once
        // Atlas low-level interface
        NVM_FLUSH(&SLL);
        InsertAfter = node;
    } else {
        InsertAfter->Next = node;
        // Atlas low-level interface
        NVM_FLUSH(&InsertAfter->Next);
        InsertAfter = node;
    }
    return node;
}

Node *insertPass2(int Num, int NumBlanks, int NumFAI) {
    Node *start_pos = InsertAfter;

    // create a single node
    Node *first_inserted = createSingleNode(Num, NumBlanks);
    Node *orig = start_pos->Next;
    Node *copy = NULL;

    if (NumFAI == 1) {
        first_inserted->Next = orig;
        InsertAfter = orig;
    } else if (orig != NULL) {
        copy = copySingleNode(orig);
        first_inserted->Next = copy;
        orig = orig->Next;
    } else {
        first_inserted->Next = NULL;
        NumFAI = 0;
        InsertAfter = first_inserted;
    }

    flushNode(first_inserted);

    for (int i = 1; i < NumFAI; ++i) {
        Node *inserted = createSingleNode(Num + i, NumBlanks);
        copy->Next = inserted;
        flushNode(copy);
        if (orig == NULL || i == NumFAI - 1) {
            inserted->Next = orig;
            InsertAfter = orig;
        } else {
            copy = copySingleNode(orig);
            inserted->Next = copy;
            orig = orig->Next;
        }
        flushNode(inserted);

        if (InsertAfter == NULL) {
            InsertAfter = inserted;
            break;
        }
    }

    start_pos->Next = first_inserted;
    // Atlas low-level interface
    NVM_FLUSH(&start_pos->Next);

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

    // Atlas initialization to be able to call low-level interfaces
    NVM_Initialize();

    int N = atoi(argv[1]);
    int K = atoi(argv[2]);
    int X = atoi(argv[3]);
    fprintf(stderr, "N = %d K = %d X = %d\n", N, K, X);
    assert(!(N % 2) && "N is not even");

    int i;
    for (i = 1; i < N / 2 + 1; ++i) {
        insertPass1(i, K);
    }
    InsertAfter = SLL;

    for (i = N / 2 + 1; i <= N; i += X) {
        insertPass2(i, K, X);
    }

    fprintf(stderr, "Sum of elements is %ld\n", printSLL());

    gettimeofday(&tv_end, NULL);

    fprintf(stderr, "time elapsed %ld us\n",
            tv_end.tv_usec - tv_start.tv_usec +
                (tv_end.tv_sec - tv_start.tv_sec) * 1000000);

    // Atlas finalization
    NVM_Finalize();

    return 0;
}
