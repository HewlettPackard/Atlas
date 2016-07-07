#include "graph.h"

#include <iostream>
#include <set>
#include <unordered_set>

using namespace std;

int main() {
  Graph g = Graph();
  for (int i = 0; i < 6; ++i) {
    g.addVertex(to_string(i));
  }

  unordered_set<string> verts =  g.getVertices();
  for (auto& v : verts) {
    cout << " " << v;
  }
  cout << endl;

  g.addEdge("0","1",5);
  g.addEdge("0","5",2);
  g.addEdge("1","2",4);
  g.addEdge("2","3",9);
  g.addEdge("3","4",7);
  g.addEdge("3","5",3);
  g.addEdge("4","0",1);
  g.addEdge("5","4",8);
  g.addEdge("5","2",1);

  set<Vertex> vos = g.getVertexObjects();
  for (auto v : vos) {
    unordered_set<string> nbrs = v.getConnections();
    for (auto& w : nbrs) {
      cout << "(" << v.getId() << ", " << w << ")\n";
    }
  }
  cout << endl;

  return 0;
}
