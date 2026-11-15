// boost/graph/connected_components.hpp : à la place de Boost.Graph, le seul usage de PlaneGCS (GCS.cpp,
// System::initSolution) : un graphe non orienté par listes, add_vertex, add_edge, num_vertices et
// connected_components, ici par union-find. Les composantes sont numérotées dans l'ordre de leur premier sommet,
// comme le parcours en profondeur de Boost (vecS) : la numérotation ne change pas les sous-systèmes de PlaneGCS.
#pragma once
#include <cstddef>
#include <utility>
#include <vector>

namespace boost
{
struct vecS
{};
struct undirectedS
{};

template<class OutEdgeList = vecS, class VertexList = vecS, class Directed = undirectedS>
class adjacency_list
{
public:
    std::vector<std::pair<int, int>> edges;
    std::size_t vertices = 0;
};

template<class A, class B, class C>
inline std::size_t add_vertex(adjacency_list<A, B, C>& g)
{
    return g.vertices++;
}

template<class A, class B, class C>
inline void add_edge(int u, int v, adjacency_list<A, B, C>& g)
{
    g.edges.emplace_back(u, v);
}

template<class A, class B, class C>
inline std::size_t num_vertices(const adjacency_list<A, B, C>& g)
{
    return g.vertices;
}

template<class A, class B, class C, class Out>
int connected_components(const adjacency_list<A, B, C>& g, Out comp)
{
    const int n = int(g.vertices);
    std::vector<int> parent(n);
    for (int i = 0; i < n; i++) {
        parent[i] = i;
    }
    auto find = [&parent](int x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    for (const auto& e : g.edges) {
        int a = find(e.first), b = find(e.second);
        if (a != b) {
            parent[a < b ? b : a] = a < b ? a : b;
        }
    }
    std::vector<int> label(n, -1);
    int count = 0;
    for (int i = 0; i < n; i++) {
        int r = find(i);
        if (label[r] < 0) {
            label[r] = count++;
        }
        comp[i] = label[r];
    }
    return count;
}
}  // namespace boost
