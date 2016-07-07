#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace std;

class Vertex {
private:
  string id;
  unordered_map<string, double> connectedTo;

public:
  Vertex(string key) : id(key), connectedTo{} {}

  void addNeighbor(string nbr, double weight) {
    connectedTo[nbr] =  weight;
  }

  ostream& operator<<(ostream& outs) {
    outs << id << " connectedTo:";
    for (auto& n : connectedTo) {
      outs << " " << n.first;
    } 
    return outs;
  }

  unordered_set<string> getConnections() {
    unordered_set<string> nbrs;
    for (auto& n : connectedTo) {
      nbrs.insert(n.first);
    }
    return nbrs;
  }

  string getId() {
    return id;
  }

  double getWeight(string nbr) {
    return connectedTo[nbr];
  }
};
