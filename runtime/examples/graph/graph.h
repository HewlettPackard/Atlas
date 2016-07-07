#include "vertex.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <unordered_map>
#include <set>

using namespace std;

class Graph {
private:
  unordered_map<string, Vertex&> vertList;
  int numVertices;

public:
  Graph() : numVertices(0) {}

  Vertex addVertex(string key) {
    ++numVertices;
    Vertex newVertex = Vertex(key);
    vertList[key] = newVertex;
    return newVertex;
  }

  Vertex getVertex(string key) {
    return vertList.at(key);
  }

  bool contains(string key) {
    return vertList.find(key) != vertList.end();
  }

  void addEdge(string f, string t, double cost = 0.0) {
    if (!contains(f)) {
      addVertex(f);
    }
    if (!contains(t)) {
      addVertex(t);
    }
    vertList[f].addNeighbor(t, cost);
  }

  unordered_set<string> getVertices() {
    unordered_set<string> vertices;
    for (auto& n : vertList) {
      vertices.insert(n.first);
    }
    return vertices;
  }


  set<Vertex> getVertexObjects() {
    set<Vertex> vos;
    for (auto& v : vertList) {
      vos.insert(v.second);
    }
    return vos;
  }
};
