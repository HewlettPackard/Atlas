#ifndef CONTRAPTION_H
#define CONTRAPTION_H

#include <pthread.h>
#include <array>
#include <cstddef>
#include <map>
#include <string>
#include <queue>

using namespace std;

enum { /* maximum sizes */
       NVERTEX = 1048576,
       NTHREADS = 13, // 1 snapshot thread and rest worker threads
};

enum { /* states */
       NORMAL,
       SNAPSHOT,
       CLEANUP,
};

enum { /* log opcodes */
       INSERT_VERTEX,
       INSERT_EDGE,
       DELETE_VERTEX,
       DELETE_EDGE,
       UPDATE_INFO,
};

typedef size_t ID;

typedef struct Edge Edge;
struct Edge {
    ID from, to;
    string eInfo;
};

typedef struct Vertex Vertex;
struct Vertex {
    ID id;
    vector<Edge *> inEdges;
    vector<Edge *> outEdges;
    string vInfo;
};

typedef struct LogEntry LogEntry;
struct LogEntry {
    ID id;
    size_t opcode;
    void *logInfo;
};

typedef struct Node Node;
struct Node {
    pthread_mutex_t nm;

    Vertex *vertex;
    queue<LogEntry *> log;
};

typedef struct Graph Graph;
struct Graph { /* entry point for data structure */
    pthread_mutex_t gm;
    array<Node, NVERTEX> nodetab;
    size_t state;
    size_t node_max;

    pthread_mutex_t tm[NTHREADS-1];
    pthread_cond_t tc[NTHREADS-1];
    size_t thread_state[NTHREADS-1];
};

/* low level APIs, locks are only acquired at higher level APIs */

void change_state(Graph *g, unsigned state);
/* Pseudocode:
 *  lock(g->gm)
 *  g->state = state
 *  unlock(g->gm)
 *  for (i : NTHREADS) {
 *      lock(g->tm[i])
 *      wait until (g->thread_state[i] == state) // TODO how to wait
 *      unlock(g->tm[i])
 *  }
 */

/**
 * Take consistent snapshot of the given graph
 * @param  g [description]
 */
void snapshot(Graph *g);
/* Pseudocode:
 *  wait until g->state == NORMAL
 *  change_state(SNAPSHOT)
 *  for (i : NVERTEX) {
 *      copy nodetab[i]->vertex // TODO decide on an output format
 *  }
 *  change_state(CLEANUP)
 *  for (i : NVERTEX) {
 *      if (nodetab[i]->log)
 *          cleanup(nodetab[i]->log) // TODO can parallelize
 *  }
 *  change_state(NORMAL)
 */

/**
 * Upserts a vertex obtained from get_vertex() or created anew
 * @param  g [description]
 * @param  v [description]
 * @param  e [description]
 * @return   [description]
 */
void upsert(Graph *g, Vertex *v);
/* Pseudocode:
 *  if (!snapshotInProgress()) {
 *      cleanup(nodetab[v->id]);
 *      nodetab[v->id] = v;
 *  } else {
 *      // TODO create logEntry le
 *      append(nodetab[v->id], le) // TODO implementation?
 *  }
 */

/* This probably is not needed as an edge can be obtained from a vertex */
/* Edge *get_edge(Graph *g, ID from, ID to); */

/**
 * Returns pointer to vertex for modification by higher level APIs
 * @param  g  [description]
 * @param  id [description]
 * @return    [description]
 */
Vertex *get_vertex(Graph *g, ID id);
/* Pseudocode:
 *  if (!snapshotInProgress()) {
 *      cleanup(nodetab[id]);
 *      return nodetab[id].vertex
 *  } else {
 *      // TODO read from both vertex and log, return updated vertex pointer
 *  }
 */

void erase_edge(Graph *g, ID id, ID from, ID to);

/*----------------------------------------------------------------------------*/

/* high level APIs:
 *  responsible for taking locks, serializing txns, updating
 */

void init_graph(Graph *);

void insert_edge(Graph *g, ID, ID);
Vertex *create_vertex(Graph *g, ID);
void print_graph(Graph *);
void print_vertex(Vertex *);
size_t graph_size(Graph *);
void delete_vertex(Graph *, ID);

// returns number of edges in the snapshot
size_t dump_snapshot(Graph *);

void thread_end_loop(Graph *, size_t);

LogEntry *create_log(Graph *g, ID);
LogEntry *get_log(Graph *g, ID);

void cleanup_vertex(Graph *g, ID);

#endif /* CONTRAPTION_H */
