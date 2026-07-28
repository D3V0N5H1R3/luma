// Graph traversal algorithms — BFS/DFS ordering, adjacency-list conversion,
// and connected components.  Registered via register_graph_traversal().

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <queue>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/collections/graph_module.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"

namespace luma {

namespace {

// BFS over `g` from `start`, appending every newly-reached vertex (in BFS
// order) to `out` and marking it in `visited`.  Vertices already in `visited`
// are skipped, so connected_components can accumulate one component per call
// while sharing a single visited set across the whole graph, and
// breadth_first_search can pass a fresh set to get the full traversal order.
void bfs_reachable(const GraphValue& g, const std::string& start, std::set<std::string>& visited,
                   std::vector<std::string>& out) {
    std::queue<std::string> q;
    q.push(start);

    visited.insert(start);

    while (!q.empty()) {
        auto cur = q.front();

        q.pop();

        out.push_back(cur);

        auto it = g.adjacency.find(cur);

        if (it != g.adjacency.end()) {
            for (const auto& e : it->second) {
                if (!visited.contains(e.to)) {
                    visited.insert(e.to);
                    q.push(e.to);
                }
            }
        }
    }
}

// Build an undirected adjacency view: every edge contributes both endpoints as
// neighbours of each other.  Used by the connectivity / bipartite / tree
// predicates, which treat a directed graph as weakly connected (i.e. ignore
// edge direction).  A std::map keeps iteration deterministic across platforms.
[[nodiscard]] std::map<std::string, std::set<std::string>> undirected_view(const GraphValue& g) {
    std::map<std::string, std::set<std::string>> view;

    for (const auto& [v, edges] : g.adjacency) {
        view[v];

        for (const auto& e : edges) {
            view[v].insert(e.to);
            view[e.to].insert(v);
        }
    }

    return view;
}

} // namespace

// Traversals — BFS/DFS ordering, adjacency-list conversion, and connected
// components (undirected reachability via BFS).
void register_graph_traversal(const EnvPtr& env) {
    ModuleBuilder{"Graph", env} // BFS traversal — returns array of vertex names in BFS order.
        .func("breadth_first_search", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.breadth_first_search", loc);

            const auto& start = expect_string(args[1], "Graph.breadth_first_search", loc);

            if (!g->adjacency.contains(start)) {
                return make_failure_value(std::string{"start vertex not found"});
            }

            std::set<std::string> visited;
            std::vector<std::string> order;

            bfs_reachable(*g, start, visited, order);

            auto arr = std::make_shared<ArrayValue>();

            for (const auto& v : order) {
                arr->elements->emplace_back(v);
            }

            return make_success_value(Value{std::move(arr)});
        })
        // DFS traversal — returns array of vertex names in DFS order.
        .func("depth_first_search", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.depth_first_search", loc);

            const auto& start = expect_string(args[1], "Graph.depth_first_search", loc);

            if (!g->adjacency.contains(start)) {
                return make_failure_value(std::string{"start vertex not found"});
            }

            auto arr = std::make_shared<ArrayValue>();

            std::set<std::string> visited;

            std::vector<std::string> stack;
            stack.push_back(start);

            while (!stack.empty()) {
                auto cur = stack.back();

                stack.pop_back();

                if (visited.contains(cur)) {
                    continue;
                }

                visited.insert(cur);

                arr->elements->emplace_back(cur);

                auto it = g->adjacency.find(cur);

                if (it != g->adjacency.end()) {
                    // Push in reverse to get alphabetical-ish order.
                    for (auto& rit : std::views::reverse(it->second)) {
                        if (!visited.contains(rit.to)) {
                            stack.push_back(rit.to);
                        }
                    }
                }
            }

            return make_success_value(Value{std::move(arr)});
        })
        .func("to_adjacency_list", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.to_adjacency_list", loc);
            auto dict = std::make_shared<DictionaryValue>();
            // Pre-build the empty hash index so each set() below is O(1), keeping
            // the build O(V) rather than O(V^2).
            dict->rebuild_index();

            for (const auto& [v, edges] : g->adjacency) {
                auto arr = std::make_shared<ArrayValue>();

                // Cap each vertex's list at max_array_size: an undirected self-loop
                // pushes two physical adjacency entries per logical edge, so a single
                // vertex's list can reach ~2x max_graph_edges while the add_edge cap
                // passes, which would otherwise build an over-limit array.
                for (const auto& e : edges) {
                    validate_container_size(arr->elements->size(), ResourceLimits::max_array_size,
                                            "Graph.to_adjacency_list", loc);
                    arr->elements->emplace_back(e.to);
                }

                dict->set(v, Value{std::move(arr)});
            }

            return Value{std::move(dict)};
        })
        // Connected components (undirected graphs only).
        // Returns result<array<array<string>>>: failure if graph is directed.
        .func("connected_components", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.connected_components", loc);

            if (g->directed) {
                return make_failure_value(
                    std::string{"Graph.connected_components: graph must be undirected"});
            }

            std::set<std::string> visited;

            auto components = std::make_shared<ArrayValue>();

            for (const auto& [start, _] : g->adjacency) {
                if (visited.contains(start)) {
                    continue;
                }

                std::vector<std::string> members;

                bfs_reachable(*g, start, visited, members);

                auto component = std::make_shared<ArrayValue>();

                for (const auto& v : members) {
                    component->elements->emplace_back(v);
                }

                components->elements->emplace_back(std::move(component));
            }

            return make_success_value(Value{std::move(components)});
        })
        // has_path — direction-aware reachability from `from` to `to` without
        // materialising the path.  Fails only if a vertex is absent; unlike
        // shortest_path it is weight-agnostic (a plain BFS walk).
        .func("has_path", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.has_path", loc);

            const auto& from = expect_string(args[1], "Graph.has_path", loc);
            const auto& to = expect_string(args[2], "Graph.has_path", loc);

            if (!g->adjacency.contains(from) || !g->adjacency.contains(to)) {
                return make_failure_value(std::string{"vertex not found"});
            }

            std::set<std::string> visited;
            std::vector<std::string> order;

            bfs_reachable(*g, from, visited, order);

            return make_success_value(Value{visited.contains(to)});
        })
        // is_connected — every vertex lies in a single component.  Undirected
        // graphs use plain reachability; directed graphs are tested for *weak*
        // connectivity (edge direction ignored).  A graph with 0 or 1 vertices
        // is trivially connected.
        .func("is_connected", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.is_connected", loc);

            if (g->adjacency.size() <= 1) {
                return make_success_value(Value{true});
            }

            const auto view = undirected_view(*g);

            // BFS from an arbitrary vertex over the undirected view; connected iff
            // every vertex is reached.
            std::set<std::string> visited;
            std::vector<std::string> stack;

            const auto& start = view.begin()->first;
            stack.push_back(start);
            visited.insert(start);

            while (!stack.empty()) {
                const auto cur = stack.back();
                stack.pop_back();

                for (const auto& n : view.at(cur)) {
                    if (!visited.contains(n)) {
                        visited.insert(n);
                        stack.push_back(n);
                    }
                }
            }

            return make_success_value(Value{visited.size() == view.size()});
        })
        // is_bipartite — the vertices can be 2-coloured so no edge joins same
        // colours (BFS colouring over the undirected view, per component).
        .func("is_bipartite", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.is_bipartite", loc);

            const auto view = undirected_view(*g);

            std::map<std::string, int> colour;

            for (const auto& [start, _] : view) {
                if (colour.contains(start)) {
                    continue;
                }

                colour[start] = 0;

                std::vector<std::string> stack{start};

                while (!stack.empty()) {
                    const auto cur = stack.back();
                    stack.pop_back();

                    const int next = colour.at(cur) ^ 1;

                    for (const auto& n : view.at(cur)) {
                        auto it = colour.find(n);

                        if (it == colour.end()) {
                            colour[n] = next;
                            stack.push_back(n);
                        } else if (it->second == colour.at(cur)) {
                            return make_success_value(Value{false});
                        }
                    }
                }
            }

            return make_success_value(Value{true});
        })
        // is_tree — connected and acyclic.  For an undirected graph a connected
        // graph is a tree exactly when edge_count == vertex_count - 1; the same
        // count test is applied to the weakly-connected view of a directed graph.
        .func("is_tree", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.is_tree", loc);

            const auto vertices = g->adjacency.size();

            // An empty graph is not a tree; a single isolated vertex is.
            if (vertices == 0) {
                return make_success_value(Value{false});
            }

            if (g->logical_edge_count() != vertices - 1) {
                return make_success_value(Value{false});
            }

            // With exactly V-1 edges, connectivity implies acyclicity, so a single
            // connectivity check suffices.
            const auto view = undirected_view(*g);

            std::set<std::string> visited;
            std::vector<std::string> stack;

            const auto& start = view.begin()->first;
            stack.push_back(start);
            visited.insert(start);

            while (!stack.empty()) {
                const auto cur = stack.back();
                stack.pop_back();

                for (const auto& n : view.at(cur)) {
                    if (!visited.contains(n)) {
                        visited.insert(n);
                        stack.push_back(n);
                    }
                }
            }

            return make_success_value(Value{visited.size() == view.size()});
        });
}

} // namespace luma
