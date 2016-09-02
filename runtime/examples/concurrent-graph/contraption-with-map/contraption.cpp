#include <algorithm>
#include <cassert>
#include <iostream>
#include <set>

#include "contraption.hpp"

static void init_node(Node *n) {
    pthread_mutexattr_t attr;

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(&n->nm, &attr);

    pthread_mutex_lock(&n->nm);
    n->vertex = 0;
    pthread_mutex_unlock(&n->nm);
}

void init_graph(Graph *g) {
    pthread_mutexattr_t attr;

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(&g->gm, &attr);

    pthread_mutex_lock(&g->gm);
    g->state = NORMAL;
    g->node_count = 0;
    pthread_mutex_unlock(&g->gm);

    for (size_t i = 0; i < NTHREADS - 1; i++) {
        pthread_mutex_init(&g->tm[i], &attr);
        g->tc[i] = PTHREAD_COND_INITIALIZER;
        pthread_mutex_lock(&g->tm[i]);
        g->thread_state[i] = NORMAL;
        pthread_mutex_unlock(&g->tm[i]);
    }
}

extern pthread_key_t tid_key;

static Vertex *create_or_get_vertex(Graph *g, string name) {
    Vertex *v;
    auto id_iter = g->map.find(name);
    if (id_iter == g->map.end()) {
        v = create_vertex(g, name);
    } else {
        pthread_mutex_lock(&g->nodetab[id_iter->second]->nm);
        v = get_vertex(g, id_iter->second);
        pthread_mutex_unlock(&g->nodetab[id_iter->second]->nm);
    }
    return v;
}

static LogEntry *create_or_get_log(Graph *g, string name) {
    LogEntry *le;
    auto id_iter = g->map.find(name);
    if (id_iter == g->map.end()) {
        le = create_log(g, name);
    } else {
        pthread_mutex_lock(&g->nodetab[id_iter->second]->nm);
        le = get_log(g, id_iter->second);
        pthread_mutex_unlock(&g->nodetab[id_iter->second]->nm);
    }
    return le;
}

void insert_edge(Graph *g, const string &n1, const string &n2) {
    size_t tid = (size_t)pthread_getspecific(tid_key);
    size_t state;

    pthread_mutex_lock(&g->gm);
    // check for state change
    pthread_mutex_lock(&g->tm[tid]);
    if (g->thread_state[tid] != g->state) {
        g->thread_state[tid] = g->state;
        pthread_cond_broadcast(&g->tc[tid]);
    }
    state = g->thread_state[tid];
    pthread_mutex_unlock(&g->tm[tid]);

    if (state == NORMAL || state == CLEANUP) {
        // get_vertex takes care of cleanup as well
        Vertex *v1 = create_or_get_vertex(g, n1);
        pthread_mutex_t *m1 = &(g->nodetab[v1->id]->nm);

        Vertex *v2 = create_or_get_vertex(g, n2);
        pthread_mutex_t *m2 = &(g->nodetab[v2->id]->nm);

        // Once pointers to vertices are in hand, no need to have global lock
        pthread_mutex_unlock(&g->gm);

        assert(v1->id != v2->id);

        if (v1->id < v2->id) {
            pthread_mutex_lock(m1);
            pthread_mutex_lock(m2);
        } else {
            pthread_mutex_lock(m2);
            pthread_mutex_lock(m1);
        }

        // check for possible duplicate edge
        for (auto &edge : v1->outEdges) {
            if (edge->to == v2->id) {
                if (v1->id < v2->id) {
                    pthread_mutex_unlock(m2);
                    pthread_mutex_unlock(m1);
                } else {
                    pthread_mutex_unlock(m1);
                    pthread_mutex_unlock(m2);
                }
                // edge already exits, nothing more to do
                return;
            }
        }

        Edge *e = new Edge;
        e->from = v1->id;
        e->to = v2->id;
        // e->eInfo = 0;

        // cout << "created edge" << endl;

        v1->outEdges.push_back(e);
        v2->inEdges.push_back(e);

        if (v1->id < v2->id) {
            pthread_mutex_unlock(m2);
            pthread_mutex_unlock(m1);
        } else {
            pthread_mutex_unlock(m1);
            pthread_mutex_unlock(m2);
        }

    } else if (state == SNAPSHOT) {
        LogEntry *le1 = create_or_get_log(g, n1);
        ID id1 = le1->id;
        pthread_mutex_t *m1 = &(g->nodetab[id1]->nm);

        LogEntry *le2 = create_or_get_log(g, n2);
        ID id2 = le2->id;
        pthread_mutex_t *m2 = &(g->nodetab[id2]->nm);

        // Once pointers to vertices are in hand, no need to have global lock
        pthread_mutex_unlock(&g->gm);

        assert(id1 != id2);

        if (id1 < id2) {
            pthread_mutex_lock(m1);
            pthread_mutex_lock(m2);
        } else {
            pthread_mutex_lock(m2);
            pthread_mutex_lock(m1);
        }

        Edge *e = new Edge;
        e->from = id1;
        e->to = id2;
        // e->eInfo = 0;

        le1->opcode = INSERT_EDGE;
        le1->logInfo = (void *)e;

        le2->opcode = INSERT_EDGE;
        le2->logInfo = (void *)e;

        if (id1 < id2) {
            pthread_mutex_unlock(m2);
            pthread_mutex_unlock(m1);
        } else {
            pthread_mutex_unlock(m1);
            pthread_mutex_unlock(m2);
        }
    } else {
        assert(false);
    }
}

Vertex *create_vertex(Graph *g, const string &name) {
    ID id = g->node_count++;

    Vertex *v = new Vertex();
    v->id = id;
    v->vInfo = name;

    // create corresponding node as well
    Node *n = new Node;
    init_node(n);
    n->vertex = v;

    // parent already has global lock
    g->nodetab[id] = n;

    assert(g->map.find(name) == g->map.end());
    g->map[name] = id;
    return v;
}

LogEntry *create_log(Graph *g, const string &name) {
    ID id = g->node_count++;
    Vertex *v = new Vertex();
    v->id = id;
    v->vInfo = name;

    // create corresponding node as well
    Node *n = new Node;
    init_node(n);

    // need to create a logEntry for a vertex first
    LogEntry *le = new LogEntry;
    le->id = id;
    le->logInfo = (void *)v; // TODO think if this should be done by caller
    le->opcode = INSERT_VERTEX;
    n->log.push(le);

    // parent already has global lock
    g->nodetab[id] = n;

    assert(g->map.find(name) == g->map.end());
    g->map[name] = id;
    return get_log(g, id);
}

Vertex *get_vertex(Graph *g, ID id) {
    Node *node = g->nodetab[id];
    if (!node->log.empty()) {
        cleanup_vertex(g, id);
    }
    return node->vertex;
}

LogEntry *get_log(Graph *g, ID id) {
    Node *node = g->nodetab[id];

    LogEntry *le = new LogEntry;
    le->id = id;
    node->log.push(le);

    return node->log.back();
}

void print_graph(Graph *g) {
    // TODO think about fine-grained locking here
    pthread_mutex_lock(&g->gm);
    for (auto node : g->nodetab) {
        pthread_mutex_lock(&node->nm);
        print_vertex(node->vertex);
        pthread_mutex_unlock(&node->nm);
    }
    pthread_mutex_unlock(&g->gm);
}

void print_vertex(Vertex *v) {
    // cout << "v: " << v << endl;
    cout << "ID: " << 1 + v->id << " name: " << v->vInfo << endl;
    cout << "inEdges: ";
    for (auto e : v->inEdges) {
        cout << "(" << 1 + e->from << ", " << 1 + e->to << e->eInfo << "), ";
    }
    cout << endl;

    cout << "outEdges: ";
    for (auto e : v->outEdges) {
        cout << "(" << 1 + e->from << ", " << 1 + e->to << e->eInfo << "), ";
    }
    cout << endl;
}

size_t graph_size(Graph *g) {
    pthread_mutex_lock(&g->gm);
    size_t size = g->node_count;
    pthread_mutex_unlock(&g->gm);
    // XXX may already be outdated by the time of return
    return size;
}

void delete_vertex(Graph *g, ID id) {
    pthread_mutex_lock(&g->gm);
    Vertex *v = get_vertex(g, id);
    pthread_mutex_unlock(&g->gm);

    // cout << "Deleting v: " << v << endl;

    set<ID> lockorder;
    while (true) {
        // 1. exploration phase
        // cout << "===Exploration\n";
        pthread_mutex_lock(&g->nodetab[id]->nm);

        // cout << "\nReading inEdges of " << id;
        for (auto ie : v->inEdges) {
            lockorder.insert(ie->from);
        }
        // cout << "\nReading outEdges of " << id;
        for (auto oe : v->outEdges) {
            lockorder.insert(oe->to);
        }
        // cout << "Gave up the lock on " << id << endl;
        pthread_mutex_unlock(&g->nodetab[id]->nm);
        lockorder.insert(id);

        // 2. lock phase
        // cout << "===Locking\n";
        for (auto e : lockorder) {
            // cout << "Locking " << e << endl;
            pthread_mutex_lock(&g->nodetab[e]->nm);
        }

        // 3. confirm phase
        // cout << "===Confirmation\n";
        set<ID> confirmorder;
        // cout << "\nReReading inEdges of " << id;
        for (auto ie : v->inEdges) {
            confirmorder.insert(ie->from);
        }
        // cout << "\nReReading outEdges of " << id;
        for (auto oe : v->outEdges) {
            confirmorder.insert(oe->to);
        }
        confirmorder.insert(id);
        set<ID> diff;
        set_symmetric_difference(lockorder.begin(), lockorder.end(),
                                 confirmorder.begin(), confirmorder.end(),
                                 inserter(diff, diff.begin()));

        if (0 != diff.size()) {
            for (auto it = lockorder.rbegin(); it != lockorder.rend(); ++it) {
                // cout << "Unlocking " << *it << endl;
                pthread_mutex_unlock(&g->nodetab[*it]->nm);
            }
            lockorder.clear();
            cout << "Restarting locking procedure\n";
            continue;
        } else {
            break;
        }
    }

    // cout << "Deleting vertex " << id << endl;
    // cout << "DEBUG1: " << &(v->inEdges) << endl;
    while (!v->inEdges.empty()) {
        // cout << "\nBefore back() in.size: " << v->inEdges.size();
        auto lasti = v->inEdges.back();
        // cout << "\nAfter back() in.size: " << v->inEdges.size();
        erase_edge(g, lasti->from, lasti->from, id);
        // cout << "Unlocking " << last->from << endl;
        // pthread_mutex_unlock(&g->nodetab[last->from]->nm);

        delete lasti;
        v->inEdges.pop_back();
    }
    assert(0 == v->inEdges.size());

    while (!v->outEdges.empty()) {
        // cout << "\nBefore back() out.size: " << v->outEdges.size();
        auto lasto = v->outEdges.back();
        // cout << "\nAfter back() out.size: " << v->outEdges.size();
        erase_edge(g, lasto->to, id, lasto->to);
        // cout << "Unlocking " << last->from << endl;
        // pthread_mutex_unlock(&g->nodetab[last->from]->nm);

        delete lasto;
        v->outEdges.pop_back();
    }
    assert(0 == v->outEdges.size());

    for (auto it = lockorder.rbegin(); it != lockorder.rend(); ++it) {
        // cout << "Unlocking " << *it << endl;
        pthread_mutex_unlock(&g->nodetab[*it]->nm);
    }

    delete v;
    assert(g->nodetab[id]->log.empty());
}

void erase_edge(Graph *g, ID id, ID from, ID to) {
    if (id == to) {
        auto &inEdges = g->nodetab[id]->vertex->inEdges;
        // cout << "DEBUG2: " << id << from << to << &(inEdges) << endl;
        for (auto it = inEdges.begin(); it != inEdges.end(); ++it) {
            if ((*it)->from == from) {
                inEdges.erase(it);
                break;
            }
        }
        // cout << "DEBUG3: " << id << from << to << &(inEdges) << endl;
    } else { // id == from
        auto &outEdges = g->nodetab[id]->vertex->outEdges;
        for (auto it = outEdges.begin(); it != outEdges.end(); ++it) {
            if ((*it)->to == to) {
                outEdges.erase(it);
                break;
            }
        }
    }
}

void change_state(Graph *g, unsigned state) {
    pthread_mutex_lock(&g->gm);
    g->state = state;

    pthread_mutex_unlock(&g->gm);

    for (unsigned i = 0; i < NTHREADS - 1; ++i) {
        pthread_mutex_lock(&g->tm[i]);
        while (g->thread_state[i] != state) {
            pthread_cond_wait(&g->tc[i], &g->tm[i]);
        }
        pthread_mutex_unlock(&g->tm[i]);
    }
}

size_t dump_snapshot(Graph *g) {
    // TODO wait until state is NORMAL
    pthread_mutex_lock(&g->gm);
    size_t table_size = g->node_count;
    assert(NORMAL == g->state);
    pthread_mutex_unlock(&g->gm);

    // std::cout << "===SNAPSHOT phase" << std::endl;
    change_state(g, SNAPSHOT);

    // choosing CSR format for snapshot dump
    vector<long> F = {0};   // Stores neighbor list for each vertex
    vector<size_t> N = {0}; // Starting points in F for each vertex

    for (size_t i = 0; i < table_size; i++) {
        auto node = g->nodetab[i];
        if (!node->vertex) {
            // skipping vertex, not consistent yet
            N.push_back(0);
            continue;
        }
        N.push_back(F.size());
        for (auto &oe : node->vertex->outEdges) {
            F.push_back((long)(1 + oe->to));
        }
        for (auto &ie : node->vertex->inEdges) {
            // negate to represent incoming edge
            // XXX might overflow
            F.push_back(-(long)(1 + ie->from));
        }
    }
    N.push_back(F.size()); // sentry

    cout << "F: " << F.size() << " N: " << N.size() << endl;

    // std::cout << "===CLEANUP phase" << std::endl;
    change_state(g, CLEANUP);
    // std::cout << "===NORMAL phase" << std::endl;
    change_state(g, NORMAL);

    return F.size() - 1;
}

void thread_end_loop(Graph *g, size_t tid) {
    pthread_mutex_lock(&g->gm);
    pthread_mutex_lock(&g->tm[tid]);
    if (g->thread_state[tid] != g->state) {
        g->thread_state[tid] = g->state;
        if (g->state == CLEANUP) {
            for (size_t i = tid; i < g->node_count; i += NTHREADS - 1) {
                pthread_mutex_lock(&g->nodetab[i]->nm);
                cleanup_vertex(g, i);
                pthread_mutex_unlock(&g->nodetab[i]->nm);
            }
        }
        pthread_cond_broadcast(&g->tc[tid]);
    }
    pthread_mutex_unlock(&g->tm[tid]);
    pthread_mutex_unlock(&g->gm);
}

void cleanup_vertex(Graph *g, ID id) {
    auto node = g->nodetab[id];
    if (node->log.empty()) {
        return;
    }

    while (!node->log.empty()) {
        auto &logEntry = node->log.front();
        switch (logEntry->opcode) {
        case INSERT_VERTEX:
            assert(!node->vertex);
            node->vertex = (Vertex *)logEntry->logInfo;
            break;
        case INSERT_EDGE: {
            auto new_edge = (Edge *)logEntry->logInfo;
            // check for possible duplicate edge
            bool exists = false;
            if (new_edge->from == id) {
                for (auto &edge : node->vertex->outEdges) {
                    if (edge->to == new_edge->to) {
                        assert(edge->from == id);
                        // edge already exits, nothing more to do
                        exists = true;
                        break;
                    }
                }
            } else {
                assert(new_edge->to == id);
                for (auto &edge : node->vertex->inEdges) {
                    if (edge->from == new_edge->from) {
                        assert(edge->to == id);
                        // edge already exits, nothing more to do
                        exists = true;
                        break;
                    }
                }
            }

            if (!exists) {
                if (id == new_edge->from) {
                    node->vertex->outEdges.push_back(new_edge);
                } else {
                    assert(id == new_edge->to);
                    node->vertex->inEdges.push_back(new_edge);
                }
            }
            break;
        }
        case DELETE_EDGE:
        case UPDATE_INFO:
        case DELETE_VERTEX:
        default:
            cerr << "ERROR: Not Yet Implemented\n";
        }
        node->log.pop();
        delete logEntry;
    }
}
