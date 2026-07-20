// Graph weighted-path algorithms — Dijkstra shortest path, Kruskal MST, and
// Floyd-Warshall all-pairs shortest paths.  Registered via register_graph_paths().

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

// Distance sentinel for unreachable vertices in the weighted path algorithms.
constexpr double k_graph_unreachable = std::numeric_limits<double>::infinity();

// Dijkstra shortest path over `g` from `from` to `to`, both assumed present and
// all edge weights assumed non-negative (validated by the caller).  Returns the
// path (from → to inclusive) or nullopt when `to` is unreachable.
std::optional<std::vector<std::string>> dijkstra_path(const GraphValue& g, const std::string& from,
                                                      const std::string& to) {
    std::map<std::string, double> dist;
    std::map<std::string, std::string> prev;

    for (const auto& [v, _] : g.adjacency) {
        dist[v] = k_graph_unreachable;
    }

    dist[from] = 0.0;

    using PQ = std::pair<double, std::string>;

    std::priority_queue<PQ, std::vector<PQ>, std::greater<>> pq;
    pq.emplace(0.0, from);

    while (!pq.empty()) {
        auto [d, u] = pq.top();

        pq.pop();

        if (d > dist[u]) {
            continue;
        }

        if (u == to) {
            break;
        }

        auto it = g.adjacency.find(u);

        if (it != g.adjacency.end()) {
            for (const auto& e : it->second) {
                const double nd = d + e.weight;

                if (nd < dist[e.to]) {
                    dist[e.to] = nd;
                    prev[e.to] = u;

                    pq.emplace(nd, e.to);
                }
            }
        }
    }

    if (dist[to] >= k_graph_unreachable) {
        return std::nullopt;
    }

    // Reconstruct path.
    std::vector<std::string> path;

    auto cur = to;

    while (cur != from) {
        path.push_back(cur);

        cur = prev[cur];
    }

    path.push_back(from);

    std::ranges::reverse(path);

    return path;
}

// Kruskal minimum spanning tree over `g`, assumed undirected (validated by the
// caller).  Returns a new undirected graph containing the MST edges; isolated
// vertices are preserved.
std::shared_ptr<GraphValue> kruskal_mst(const GraphValue& g) {
    // Collect all edges (deduplicate for undirected).
    struct Edge {
        std::string from;
        std::string to;
        double weight;
    };

    std::vector<Edge> edges;
    std::set<std::string> vertices_set;

    for (const auto& [v, adj] : g.adjacency) {
        vertices_set.insert(v);

        for (const auto& e : adj) {
            if (v < e.to) {
                edges.push_back({.from = v, .to = e.to, .weight = e.weight});
            }
        }
    }

    // Sort edges by weight.
    std::ranges::sort(edges, [](const Edge& a, const Edge& b) { return a.weight < b.weight; });

    // Union-Find.
    std::map<std::string, std::string> parent;
    std::map<std::string, int> uf_rank;

    for (const auto& v : vertices_set) {
        parent[v] = v;
        uf_rank[v] = 0;
    }

    std::function<std::string(const std::string&)> find = [&](const std::string& x) -> std::string {
        if (parent[x] != x) {
            parent[x] = find(parent[x]);
        }

        return parent[x];
    };

    auto unite = [&](const std::string& a, const std::string& b) -> bool {
        auto ra = find(a);
        auto rb = find(b);

        if (ra == rb) {
            return false;
        }

        if (uf_rank[ra] < uf_rank[rb]) {
            std::swap(ra, rb);
        }

        parent[rb] = ra;

        if (uf_rank[ra] == uf_rank[rb]) {
            ++uf_rank[ra];
        }

        return true;
    };

    // Build MST.
    auto mst = std::make_shared<GraphValue>();
    mst->directed = false;

    for (const auto& v : vertices_set) {
        mst->adjacency[v]; // Ensure all vertices exist.
    }

    for (const auto& edge : edges) {
        if (unite(edge.from, edge.to)) {
            mst->adjacency[edge.from].push_back({.to = edge.to, .weight = edge.weight});
            mst->adjacency[edge.to].push_back({.to = edge.from, .weight = edge.weight});
        }
    }

    return mst;
}

// Result of floyd_warshall: the vertex order that indexes the matrix, the n×n
// distance matrix (k_graph_unreachable for unreachable pairs), and whether a
// negative cycle was detected.
struct AllPairsShortestPaths {
    std::vector<std::string> vertices;
    std::vector<std::vector<double>> distances;
    bool negative_cycle{false};
};

// Floyd-Warshall all-pairs shortest paths over `g`.  Returns nullopt when the
// n×n distance matrix would exceed the dictionary-size limit: max_graph_vertices
// bounds only the O(V+E) adjacency structure, so this guards the O(V²) matrix,
// letting the caller report a catchable failure instead of letting the
// allocation throw std::bad_alloc out of the VM.  The check avoids overflow by
// dividing rather than computing n*n.
std::optional<AllPairsShortestPaths> floyd_warshall(const GraphValue& g) {
    AllPairsShortestPaths result;

    for (const auto& [v, _] : g.adjacency) {
        result.vertices.push_back(v);
    }

    const auto n = result.vertices.size();

    if (n != 0 && n > ResourceLimits::max_dictionary_size / n) {
        return std::nullopt;
    }

    std::map<std::string, std::size_t> idx;

    for (std::size_t i = 0; i < n; ++i) {
        idx[result.vertices[i]] = i;
    }

    // Initialize distance matrix.
    auto& dist = result.distances;
    dist.assign(n, std::vector<double>(n, k_graph_unreachable));

    for (std::size_t i = 0; i < n; ++i) {
        dist[i][i] = 0.0;
    }

    for (const auto& [v, adj] : g.adjacency) {
        for (const auto& e : adj) {
            auto fi = idx.find(e.to);

            if (fi != idx.end()) {
                dist[idx[v]][fi->second] = e.weight;
            }
        }
    }

    // Floyd-Warshall.
    for (std::size_t k = 0; k < n; ++k) {
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = 0; j < n; ++j) {
                dist[i][j] = std::min(dist[i][k] + dist[k][j], dist[i][j]);
            }
        }
    }

    // Check for negative cycles.
    for (std::size_t i = 0; i < n; ++i) {
        if (dist[i][i] < 0.0) {
            result.negative_cycle = true;
            break;
        }
    }

    return result;
}

} // namespace

// Weighted paths — Dijkstra shortest path, Kruskal MST, and Floyd-Warshall
// all-pairs shortest paths.
void register_graph_paths(const EnvPtr& env) {
    ModuleBuilder{"Graph", env} // Shortest path — Dijkstra's algorithm.
        // Returns result<array<string>> with the path, or fail.
        .func("shortest_path", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.shortest_path", loc);

            const auto& from = expect_string(args[1], "Graph.shortest_path", loc);
            const auto& to = expect_string(args[2], "Graph.shortest_path", loc);

            if (!g->adjacency.contains(from) || !g->adjacency.contains(to)) {
                return make_failure_value(std::string{"vertex not found"});
            }

            // Validate no negative weights (Dijkstra requires non-negative).
            for (const auto& [v, edges] : g->adjacency) {
                for (const auto& e : edges) {
                    if (e.weight < 0) {
                        return make_failure_value(
                            std::string{"Graph.shortest_path: negative edge weights "
                                        "are not supported"});
                    }
                }
            }

            auto path = dijkstra_path(*g, from, to);

            if (!path) {
                return make_failure_value(std::string{"no path found"});
            }

            auto arr = std::make_shared<ArrayValue>();

            for (const auto& v : *path) {
                arr->elements->emplace_back(v);
            }

            return make_success_value(Value{std::move(arr)});
        })
        // Kruskal's MST — for undirected graphs only.
        .func("minimum_spanning_tree", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& graph = expect_graph(args[0], "Graph.minimum_spanning_tree", loc);

            if (graph->directed) {
                return make_failure_value(
                    std::string{"Graph.minimum_spanning_tree: graph must be undirected"});
            }

            return make_success_value(Value{kruskal_mst(*graph)});
        })
        // Floyd-Warshall all-pairs shortest paths.
        .func("all_pairs_shortest_paths", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& graph = expect_graph(args[0], "Graph.all_pairs_shortest_paths", loc);

            auto result = floyd_warshall(*graph);

            if (!result) {
                return make_failure_value(std::string{
                    "Graph.all_pairs_shortest_paths: too many vertices for an all-pairs matrix"});
            }

            if (result->negative_cycle) {
                return make_failure_value(
                    std::string{"Graph.all_pairs_shortest_paths: graph contains a negative cycle"});
            }

            // Convert to nested dictionary.
            const auto& vertices = result->vertices;
            const auto& dist = result->distances;
            const auto n = vertices.size();

            auto outer = std::make_shared<DictionaryValue>();
            // Pre-build the empty hash indexes so each set() below is O(1),
            // keeping the matrix build O(n^2) rather than O(n^3).
            outer->rebuild_index();

            for (std::size_t i = 0; i < n; ++i) {
                auto inner = std::make_shared<DictionaryValue>();
                inner->rebuild_index();

                for (std::size_t j = 0; j < n; ++j) {
                    if (!std::isinf(dist[i][j])) {
                        inner->set(vertices[j], Value{dist[i][j]});
                    }
                }

                outer->set(vertices[i], Value{std::move(inner)});
            }

            return make_success_value(Value{std::move(outer)});
        });
}

} // namespace luma
