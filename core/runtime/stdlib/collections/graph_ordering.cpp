// Graph ordering algorithms — cycle detection, topological sort (Kahn), and
// Tarjan's strongly connected components.  Registered via register_graph_ordering().

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

// Guard recursive graph traversals against native stack overflow.  Each DFS
// (directed/undirected cycle detection, Tarjan's SCC) calls this at entry, so
// the "path deeper than the recursion limit" invariant lives in one place
// rather than being copied into every traversal lambda.
inline void ensure_traversal_depth(int depth, std::string_view fn, const SourceLocation& loc) {
    if (depth > ResourceLimits::max_call_depth) {
        throw RuntimeError{error_msg("Graph", fn, "maximum traversal depth exceeded"), loc,
                           std::format("the graph contains a path deeper than the supported "
                                       "recursion limit of {}",
                                       ResourceLimits::max_call_depth)};
    }
}

// Tarjan's strongly connected components over `g`, assumed directed (validated
// by the caller).  Returns the SCCs, each a list of vertex names.  `loc`
// attributes the traversal-depth guard error if the recursion runs too deep.
std::vector<std::vector<std::string>> tarjan_scc(const GraphValue& g, const SourceLocation& loc) {
    int index_counter = 0;
    std::map<std::string, int> indices;
    std::map<std::string, int> lowlinks;
    std::map<std::string, bool> on_stack;
    std::vector<std::string> stack;
    std::vector<std::vector<std::string>> components;

    std::function<void(const std::string&, int)> strongconnect = [&](const std::string& v,
                                                                     int depth) {
        ensure_traversal_depth(depth, "strongly_connected_components", loc);

        indices[v] = index_counter;
        lowlinks[v] = index_counter;
        ++index_counter;
        stack.push_back(v);
        on_stack[v] = true;

        auto it = g.adjacency.find(v);

        if (it != g.adjacency.end()) {
            for (const auto& edge : it->second) {
                if (!indices.contains(edge.to)) {
                    strongconnect(edge.to, depth + 1);
                    lowlinks[v] = std::min(lowlinks[v], lowlinks[edge.to]);
                } else if (on_stack[edge.to]) {
                    lowlinks[v] = std::min(lowlinks[v], indices[edge.to]);
                }
            }
        }

        if (lowlinks[v] == indices[v]) {
            std::vector<std::string> component;

            while (true) {
                auto w = stack.back();
                stack.pop_back();
                on_stack[w] = false;
                component.push_back(w);

                if (w == v) {
                    break;
                }
            }

            components.push_back(std::move(component));
        }
    };

    for (const auto& [v, _] : g.adjacency) {
        if (!indices.contains(v)) {
            strongconnect(v, 0);
        }
    }

    return components;
}

} // namespace

// Ordering — cycle detection, topological sort (Kahn), and Tarjan's strongly
// connected components.
void register_graph_ordering(const EnvPtr& env) {
    ModuleBuilder{"Graph", env}
        // Cycle detection using DFS — works for directed and undirected graphs.
        .func("has_cycle", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.has_cycle", loc);

            if (g->directed) {
                // Directed: three-colour DFS (white=0, grey=1, black=2).
                std::map<std::string, int> colour;

                for (const auto& [v, _] : g->adjacency) {
                    colour[v] = 0;
                }

                std::function<bool(const std::string&, int)> dfs = [&](const std::string& u,
                                                                       int depth) -> bool {
                    ensure_traversal_depth(depth, "has_cycle", loc);

                    colour[u] = 1;

                    auto it = g->adjacency.find(u);

                    if (it != g->adjacency.end()) {
                        for (const auto& e : it->second) {
                            if (colour[e.to] == 1) {
                                return true;
                            }

                            if (colour[e.to] == 0 && dfs(e.to, depth + 1)) {
                                return true;
                            }
                        }
                    }

                    colour[u] = 2;

                    return false;
                };

                // Iterate roots in deterministic (sorted) order.  `adjacency` is a
                // hash map whose iteration order differs between standard libraries,
                // which would make the traversal-depth guard fire on some platforms
                // but not others; a stable root order keeps behaviour identical.  A
                // snapshot vector is used (not the ordered `colour` map) because dfs
                // mutates `colour` via operator[], which could invalidate iterators.
                std::vector<std::string> roots;
                roots.reserve(g->adjacency.size());

                for (const auto& [v, _] : g->adjacency) {
                    roots.push_back(v);
                }

                std::ranges::sort(roots);

                for (const auto& v : roots) {
                    if (colour[v] == 0 && dfs(v, 0)) {
                        return Value{true};
                    }
                }
            } else {
                // Undirected: DFS tracking the parent to skip the back-edge.
                std::set<std::string> visited;

                std::function<bool(const std::string&, std::optional<std::string>, int)> dfs =
                    [&](const std::string& u, std::optional<std::string> parent,
                        int depth) -> bool {
                    ensure_traversal_depth(depth, "has_cycle", loc);

                    visited.insert(u);

                    auto it = g->adjacency.find(u);

                    if (it != g->adjacency.end()) {
                        for (const auto& e : it->second) {
                            if (!visited.contains(e.to)) {
                                if (dfs(e.to, u, depth + 1)) {
                                    return true;
                                }
                            } else if (!parent.has_value() || e.to != *parent) {
                                return true;
                            }
                        }
                    }

                    return false;
                };

                // Iterate roots in deterministic (sorted) order (see the directed
                // branch above) so the traversal-depth guard behaves identically
                // across platforms.
                std::vector<std::string> roots;
                roots.reserve(g->adjacency.size());

                for (const auto& [v, _] : g->adjacency) {
                    roots.push_back(v);
                }

                std::ranges::sort(roots);

                for (const auto& v : roots) {
                    if (!visited.contains(v)) {
                        if (dfs(v, std::nullopt, 0)) {
                            return Value{true};
                        }
                    }
                }
            }

            return Value{false};
        })
        // Topological sort using Kahn's algorithm (directed graphs only).
        // Returns result<array<string>>: failure if graph is undirected or has a cycle.
        .func("topological_sort", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.topological_sort", loc);

            if (!g->directed) {
                return make_failure_value(
                    std::string{"Graph.topological_sort: graph must be directed"});
            }

            // Compute in-degrees.
            std::map<std::string, int> in_degree;

            for (const auto& [v, _] : g->adjacency) {
                in_degree[v] = 0;
            }

            for (const auto& [_, edges] : g->adjacency) {
                for (const auto& e : edges) {
                    in_degree[e.to]++;
                }
            }

            // Kahn's BFS.
            std::queue<std::string> q;

            for (const auto& [v, deg] : in_degree) {
                if (deg == 0) {
                    q.push(v);
                }
            }

            auto arr = std::make_shared<ArrayValue>();

            while (!q.empty()) {
                auto u = q.front();

                q.pop();

                arr->elements->emplace_back(u);

                auto it = g->adjacency.find(u);

                if (it != g->adjacency.end()) {
                    for (const auto& e : it->second) {
                        if (--in_degree[e.to] == 0) {
                            q.push(e.to);
                        }
                    }
                }
            }

            if (static_cast<std::size_t>(arr->elements->size()) != g->adjacency.size()) {
                return make_failure_value(
                    std::string{"Graph.topological_sort: graph contains a cycle"});
            }

            return make_success_value(Value{std::move(arr)});
        })
        // Tarjan's SCC — for directed graphs only.
        .func("strongly_connected_components", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& graph = expect_graph(args[0], "Graph.strongly_connected_components", loc);

            if (!graph->directed) {
                return make_failure_value(
                    std::string{"Graph.strongly_connected_components: graph must be directed"});
            }

            auto components = tarjan_scc(*graph, loc);

            // Convert to Value.
            auto result = std::make_shared<ArrayValue>();

            for (auto& comp : components) {
                auto arr = std::make_shared<ArrayValue>();

                for (auto& v : comp) {
                    arr->elements->emplace_back(std::move(v));
                }

                result->elements->emplace_back(std::move(arr));
            }

            return make_success_value(Value{std::move(result)});
        });
}

} // namespace luma
