// Graph module — aggregates the traversal, path, and ordering algorithm
// registrations (split into graph_traversal.cpp, graph_paths.cpp, and
// graph_ordering.cpp for readability).  register_graph_algorithms() is called
// from register_graph_ns().

#include "runtime/stdlib/collections/graph_module.hpp"

namespace luma {

// Traversals and graph algorithms (BFS/DFS, shortest paths, cycles, MST, SCC).
void register_graph_algorithms(const EnvPtr& env) {
    register_graph_traversal(env);
    register_graph_paths(env);
    register_graph_ordering(env);
}

} // namespace luma
