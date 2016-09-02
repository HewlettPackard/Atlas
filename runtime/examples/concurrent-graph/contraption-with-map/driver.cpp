#include <pthread.h>        // for pthread_t, pthread_create, pthread_join
#include <unistd.h>         // for usleep
#include <cassert>          // for assert
#include <cstdlib>          // for EXIT_FAILURE
#include <fstream>          // for size_t, ifstream
#include <iostream>
#include <sstream>
#include <string>           // for string

#include "contraption.hpp"  // for Graph, ::NTHREADS, graph_size, delete_vertex
#include "minunit.h"        // for mu_run_test

using namespace std;

int tests_run = 0;
enum { MAXGRAPHS = 11 };
Graph graphs[MAXGRAPHS];

// static const char *test_hello() {
//     mu_assert("error, 1 != 0", 1 == 0);
//     return 0;
// }

// static void initialize_graphs() {
//     for (size_t i = 0; i < MAXGRAPHS; i++) {
//         init_graph(&graphs[i]);
//     }
// }

static const char *input_file(Graph *g, string file) {
    ifstream input(file);
    if (!input.is_open()) {
        cerr << "Could not open test file for reading\n";
        return (const char *)EXIT_FAILURE;
    }

    string line;
    while (getline(input, line)) {
        stringstream linestream(line);
        string v1, v2;
        linestream >> v1 >> v2;
        insert_edge(g, v1, v2);
    }
    return 0;
}

// static const char *test_input() {
//     vector<string> files = {
//         "23_1", "23_2", "23_8", "23_9c", "24_1",
//         "25_2", "gg_1", "gg_2", "gg_13", "gg_250",
//     };
//     assert(files.size() <= MAXGRAPHS);
//
//     for (size_t i = 0; i < files.size(); i++) {
//         cout << "Processing file " << i << ": " << files[i] << endl;
//         input_file(&graphs[i], files[i]);
//     }
//     return 0;
// }
//
// static const char *test_output() {
//     for (size_t i = 0; i < MAXGRAPHS; i++) {
//         if (0 == graph_size(&graphs[i])) {
//             continue;
//         }
//         cout << endl << "Graph " << i << endl;
//         print_graph(&graphs[i]);
//     }
//     cout << endl;
//     return 0;
// }
//
// static const char *test_snapshot() {
//     for (size_t i = 0; i < MAXGRAPHS; i++) {
//         if (0 == graph_size(&graphs[i])) {
//             continue;
//         }
//         cout << endl << "Snapshotting graph " << i << endl;
//         dump_snapshot(&graphs[i]);
//     }
//     cout << endl;
//     return 0;
// }
//
// static const char *test_deletion() {
//     for (size_t i = 0; i < MAXGRAPHS; i++) {
//         size_t node_count = graph_size(&graphs[i]);
//
//         if (node_count > 0) {
//             cout << "Deleting all vertices from graph " << i << endl;
//         }
//
//         for (ID id = 0; id < node_count; id++) {
//             delete_vertex(&graphs[i], id);
//             // TODO might be better to do this at a lower abstraction level
//             delete graphs[i].nodetab[id];
//         }
//     }
//     return 0;
// }

Graph *multigraph; // for multithreaded test
pthread_key_t tid_key;

static void initialize_multigraph() {
    multigraph = new Graph;
    init_graph(multigraph);
}

void multiinsert(size_t i) {
    pthread_setspecific(tid_key, (const void *)i);
    vector<string> files = {"xaa", "xab", "xac", "xad", "xae", "xaf", "xag",
                            "xah", "xai", "xaj", "xak", "xal", "xam", "xan",
                            "xao", "xap", "xaq", "xar", "xas", "xat", "xau",
                            "xav", "xaw", "xax", "xay", "xaz", "xba", "xbb",
                            "xbc", "xbd", "xbe", "xbf", "xbg", "xbh", "xbi",
                            "xbj", "xbk", "xbl", "xbm"};

    input_file(multigraph, files[i]);

    // TODO need better way to exit for worker threads
    while (true) {
        thread_end_loop(multigraph, i);
    }
}

static void periodic_snapshot(unsigned useconds) {
    size_t edge_count_old = 0;
    size_t edge_count_new = 0;
    while (true) {
        usleep(useconds);
        cout << "Taking Snapshot" << endl;
        edge_count_new = dump_snapshot(multigraph);
        if (edge_count_old == edge_count_new) {
            // snapshot stabilized, we can exit
            break;
        }
        edge_count_old = edge_count_new;
    }
    cout << "Total edges in final snapshot: " << edge_count_new << endl;
}

static const char *test_multi_thread_insert() {
    static pthread_t T[NTHREADS];
    pthread_key_create(&tid_key, 0);

    for (size_t i = 0; i < NTHREADS - 1; i++) {
        int r =
            pthread_create(&T[i], 0, (void *(*)(void *))multiinsert, (void *)i);
        assert(0 == r);
    }
    // snapshot thread
    assert(0 == pthread_create(&T[NTHREADS - 1], 0,
                               (void *(*)(void *))periodic_snapshot,
                               (void *)500000)); // 0.5sec

    assert(0 == pthread_join(T[NTHREADS - 1], 0));
    // TODO workaround for clean exit until I find a way to exit looping threads
    exit(0);

    return 0;
}

// void multidelete(size_t i) {
//     size_t node_count = graph_size(multigraph);
//
//     for (ID id = i; id < node_count; id += NTHREADS - 1) {
//         // cout << "Thread " << i << " deleting " << id << endl;
//         delete_vertex(multigraph, id);
//     }
// }
//
// static void cleanup_multigraph() {
//     delete multigraph;
// }
//
// static const char *test_multi_thread_delete() {
//     cout << "Starting multi thread delete test\n\n";
//     static pthread_t T[NTHREADS - 1];
//     for (size_t i = 0; i < NTHREADS - 1; i++) {
//         int r =
//             pthread_create(&T[i], 0, (void *(*)(void *))multidelete, (void
//             *)i);
//         assert(0 == r);
//     }
//
//     for (size_t i = 0; i < NTHREADS - 1; i++) {
//         int r = pthread_join(T[i], 0);
//         assert(0 == r);
//     }
//
//     return 0;
// }

static const char *all_tests() {
    // initialize_graphs();
    // mu_run_test(test_input);
    // mu_run_test(test_output);
    // mu_run_test(test_snapshot);
    // mu_run_test(test_deletion);

    initialize_multigraph();
    mu_run_test(test_multi_thread_insert);
    // dump_snapshot(multigraph);
    // TODO come up with a good cleanup solution, memory leaks exist currently
    // mu_run_test(test_multi_thread_delete);
    // cleanup_multigraph();
    return 0;
}

int main(int argc, char const *argv[]) {
    if (argc != 1) {
        cerr << argv[0] << ": no arguments supported!\n";
        return EXIT_FAILURE;
    }

    const char *result = all_tests();
    if (result != 0) {
        cout << result << endl;
    } else {
        cout << "\nALL TESTS PASSED\n";
    }
    cout << "Tests run: " << tests_run << endl;

    return result != 0;
}
