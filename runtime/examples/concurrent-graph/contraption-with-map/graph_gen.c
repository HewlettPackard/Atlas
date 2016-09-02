/* Simple random graph generator. */
/* AUTHOR: Terence Kelly          */

#include <assert.h>   // for assert
#include <errno.h>    // for errno
#include <inttypes.h> // for PRIu32
#include <limits.h>   // for ULLONG_MAX
#include <stdint.h>   // for uint64_t, UINT32_MAX, UINT64_C, UINT64_MAX
#include <stdio.h>    // for NULL, fprintf, stderr, stdin, stdout
#include <stdlib.h>   // for strtoull

#define P (void)printf
#define FP (void)fprintf

/* maintain together: */
typedef uint32_t vertex_t;
typedef struct { vertex_t a, b; } edge_t;
#define VERTEX_T_MAX UINT32_MAX
#define PRIv PRIu32

/* obtain sane result from confusing strtoull() */
static unsigned long long int s2ull(const char *s) {
    char *p = NULL;
    unsigned long long int r;
    int old_errno = errno;
    assert(NULL != s && '\0' != *s);
    errno = 0;
    r = strtoull(s, &p, 0);
    assert(NULL != p);
    assert(p != s);
    assert('\0' == *p);
    if (0 == r || ULLONG_MAX == r) {
        assert(0 == errno);
    }
    errno = old_errno;
    return r;
}

static vertex_t s2v(const char *s) {
    unsigned long long int r;
    assert(sizeof(unsigned long long int) >= sizeof(vertex_t));
    r = s2ull(s);
    assert(0 < r);
    assert(VERTEX_T_MAX >= r);
    return (vertex_t)r;
}

static uint64_t S = UINT64_C(0xeefd0585cd91a45d); /* PRNG state */

static uint64_t prng(void) { /* XORshift */
    S ^= S >> 21;
    S ^= S << 35;
    S ^= S >> 4;
    return S;
}

static void flip(edge_t *e) {
    if (0 == prng() % 2) {
        vertex_t t = e->a;
        e->a = e->b;
        e->b = t;
    }
}

/* Return random vertex ID drawn uniformly from [1..V].
   (Note that "1 + prng() % V" does not yield uniformity,
   thus the extra bit of fuss.) */
static vertex_t rv(vertex_t V) {
    uint64_t r;
    do {
        r = prng();
    } while (r > (UINT64_MAX / V) * (uint64_t)V);
    return 1 + (vertex_t)(r % V);
}

int main(int argc, char *argv[]) {
    edge_t e;
    if (1 == argc) {
        while (1 == fread(&e, sizeof e, (size_t)1, stdin)) {
            P("%" PRIv " %" PRIv "\n", e.a, e.b);
        }
    } else if (3 == argc || 4 == argc) {
        vertex_t V;
        uint64_t E, i;
        size_t r;
        if (4 == argc) {
            S = (uint64_t)s2ull(argv[3]);
            assert(0 != S);
        }
        V = s2v(argv[1]);
        E = (uint64_t)s2ull(argv[2]);
        assert(0 < E && V - 1 <= E && (uint64_t)V * (uint64_t)(V - 1) >= E);
        /* ensure that graph is connected: create first V-1 edges by
           connecting each vertex 2..V to a randomly chosen lower-ID
           vertex; remaining edges are Erdos-Renyi */
        for (i = 1; i <= E; i++) {
            if (i < V) {
                e.a = (vertex_t)i + 1;
                e.b = rv((vertex_t)i);
            } else {
                e.a = rv(V);
                do {
                    e.b = rv(V);
                } while (e.a == e.b);
            }
            assert(0 < e.a && V >= e.a && 0 < e.b && V >= e.b);
            flip(&e);
            r = fwrite(&e, sizeof e, (size_t)1, stdout);
            assert(1 == r);
        }
    } else {
        FP(stderr,
           "two usage patterns:\n"
           "    generator:          %s  V  E  [seed]  >  binary_edge_list\n"
           "    conversion filter:  %s  <  binary_edge_list  >  "
           "ascii_edge_list\n"
           "\n"
           "notes:\n"
           "\n"
           "generator parameters:\n"
           "    V (number of vertices) must be at least 2\n"
           "    E (number of edges) must be sufficient for weakly connected "
           "graph\n"
           "        and must not exceed number of vertex pairs\n"
           "    optional 64-bit PRNG seed can be given in octal, decimal, or "
           "hex\n"
           "\n"
           "generated graph:\n"
           "    may be interpreted as directed or undirected\n"
           "    is guaranteed to be (weakly) connected\n"
           "    does not contain self-edges\n"
           "    may contain duplicate edges\n"
           "    contains an edge incident to every vertex [1..V]\n"
           "\n"
           "binary-to-ascii conversion filter:\n"
           "    does not sanity-check graph in any way\n",
           argv[0], argv[0]);
    }
    return 0;
}
