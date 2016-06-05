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
 

#ifndef DURABILITY_GRAPH_HPP
#define DURABILITY_GRAPH_HPP

#include <utility>

#include <boost/graph/graph_traits.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graphviz.hpp>

#include "helper.hpp"
#include "fase.hpp"

namespace Atlas {

// This is the durability graph built for consistency management of
// persistent data. A node denotes a failure-atomic section of code
// (FASE). An edge denotes a happens-after relationship. So if there
// is an edge from src to dest, src "happens-after" dest.
class DGraph {

public:

    // Vertex properties. A vertex, corresponding to a FASE, is stable
    // and is included in a consistent state if all FASEs that happen
    // before it are also stable and are included in the consistent
    // state. 
    struct VProp {
        VProp(FASection *fase, bool is_stable) 
            : Fase_(fase), isStable_(is_stable) {}
        VProp() = delete;
        
        FASection *Fase_;
        bool isStable_;
    };

    typedef boost::adjacency_list<
        boost::setS /* OutEdges: not allowing multi-graph */,
        boost::listS /* Vertices: don't tolerate renumbered vertex indices */,
        boost::bidirectionalS /* required for durability graph */,
        VProp> DirectedGraph;
    
    typedef boost::graph_traits<DirectedGraph>::vertex_descriptor VDesc;
    typedef boost::graph_traits<DirectedGraph>::edge_descriptor EDesc;
    typedef boost::graph_traits<DirectedGraph>::vertex_iterator VIter;
    typedef boost::graph_traits<DirectedGraph>::in_edge_iterator IEIter;

    // Status of a node in a given consistent state
    enum NodeType {kAvail, kAbsent};

    struct NodeInfo {
        NodeInfo(VDesc nid, NodeType node_type) 
            : NodeId_(nid), NodeType_(node_type) {}
        NodeInfo() = delete;
        
        VDesc NodeId_;
        NodeType NodeType_;
    };
    
    typedef std::map<LogEntry*, NodeInfo> NodeInfoMap;

    boost::graph_traits<DirectedGraph>::vertices_size_type
    get_num_vertices() const
        { return boost::num_vertices(DirectedGraph_); }
    
    void set_is_stable(VDesc vertex, bool b)
        { DirectedGraph_[vertex].isStable_ = b; }
    bool is_stable(VDesc vertex) const
        { return DirectedGraph_[vertex].isStable_; }

    void addToNodeInfoMap(LogEntry *le, VDesc nid, NodeType nt);
    NodeInfo getTargetNodeInfo(LogEntry *tgt_le);

    VDesc createNode(FASection *fase);
    EDesc createEdge(VDesc src, VDesc tgt);

    const DirectedGraph& get_directed_graph() const
        { return DirectedGraph_; }

    void clear_vertex(VDesc vertex)
        { boost::clear_vertex(vertex, DirectedGraph_); }
    void remove_vertex(VDesc vertex)
        { boost::remove_vertex(vertex, DirectedGraph_); }

    FASection *get_fase(VDesc vertex) const
        { return DirectedGraph_[vertex].Fase_; }
    
    void trace();
    template<class T> void traceHelper(T tt)
        { Helper::getInstance().trace(tt); }
    
private:

    DirectedGraph DirectedGraph_;
    NodeInfoMap NodeInfoMap_;
    
    void PrintLogs(FASection *fase);
    void PrintDummyLog(LogEntry *le);
    void PrintAcqLog(LogEntry *le);
    void PrintRdLockLog(LogEntry *le);
    void PrintWrLockLog(LogEntry *le);
    void PrintBeginDurableLog(LogEntry *le);
    void PrintRelLog(LogEntry *le);
    void PrintRWUnlockLog(LogEntry *le);
    void PrintEndDurableLog(LogEntry *le);
    void PrintStrLog(LogEntry *le);
    void PrintMemOpLog(LogEntry *le);
    void PrintStrOpLog(LogEntry *le);    
    void PrintAllocLog(LogEntry *le);
    void PrintFreeLog(LogEntry *le);
};
    
inline DGraph::VDesc DGraph::createNode(FASection *fase)
{
    // default attribute is stable. If any contained log entry is
    // unresolved, the stable bit is flipped and the corresponding node
    // removed from the graph
    return boost::add_vertex(VProp(fase, true /* stable */),
                                     DirectedGraph_);
}

inline DGraph::EDesc DGraph::createEdge(VDesc src, VDesc tgt)
{
    // TODO: are we creating a multi-graph? Or does add_edge filter
    // that out?
    std::pair<EDesc, bool> edge_pair =
        boost::add_edge(src, tgt, DirectedGraph_);
    return edge_pair.first;
}

} // end namespace

#endif
