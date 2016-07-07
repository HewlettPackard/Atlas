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

Node *createNode(int Num, int NumBlanks, __attribute__((unused)) int NumFAI) {
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
    return node;
}

Node *insertPass1(int Num, int NumBlanks, int NumFAI) {
    Node *node = createNode(Num, NumBlanks, NumFAI);

    // In pass 1, the new node is the last node in the list
    node->Next = NULL;
    if (!SLL) {
        SLL = node; // write-once
        InsertAfter = node;
    } else {
        InsertAfter->Next = node;
        InsertAfter = node;
    }
    return node;
}

Node *insertPass2(int Num, int NumBlanks, int NumFAI) {
    Node *node = createNode(Num, NumBlanks, NumFAI);

    node->Next = InsertAfter->Next;
    InsertAfter->Next = node;
    InsertAfter = node->Next;
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

    int i;
    for (i = 1; i < N / 2 + 1; ++i) {
        insertPass1(i, K, X);
    }
    InsertAfter = SLL;

    for (i = N / 2 + 1; i < N + 1; ++i) {
        insertPass2(i, K, X);
    }

    fprintf(stderr, "Sum of elements is %ld\n", printSLL());

    gettimeofday(&tv_end, NULL);

    fprintf(stderr, "time elapsed %ld us\n",
            tv_end.tv_usec - tv_start.tv_usec +
                (tv_end.tv_sec - tv_start.tv_sec) * 1000000);

    return 0;
}
