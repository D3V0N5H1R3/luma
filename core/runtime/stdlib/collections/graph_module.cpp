#include "runtime/stdlib/collections/graph_module.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <string>
#include <vector>

#include "analysis/errors/error.hpp"
#include "analysis/source/source_location.hpp"
#include "runtime/interpreter/value.hpp"
#include "runtime/stdlib/common/error_messages.hpp"
#include "runtime/stdlib/common/function_builder.hpp"
#include "runtime/stdlib/common/native_function.hpp"

namespace luma {

namespace {

// Locate the first edge in `edges` that targets `to`, or edges.end().  Shared
// by has_edge (membership), edge_weight (lookup), and remove_edge (erase) so
// the "scan a vertex's adjacency list for `to`" rule lives in one place.
[[nodiscard]] std::vector<GraphEdge>::iterator find_edge_to(std::vector<GraphEdge>& edges,
                                                            const std::string& to) {
    return std::ranges::find_if(edges, [&](const GraphEdge& e) { return e.to == to; });
}

// Adds a from->to edge (and the reverse for undirected graphs) into `g`,
// enforcing the same vertex and edge resource limits as Graph.add_edge.  Shared
// by Graph.from_edges and Graph.from_adjacency_list so the "add endpoints as
// vertices, then the edge" rule and its limit checks live in one place.
void add_edge_into(GraphValue& g, const std::string& from, const std::string& to, double weight,
                   std::string_view function_name, SourceLocation loc) {
    if (!std::isfinite(weight)) {
        throw RuntimeError{error_msg("Graph", function_name, "edge weight must be finite"), loc,
                           "NaN and ±Infinity cannot be ordered safely, so they would "
                           "break shortest-path and minimum-spanning-tree computations"};
    }

    std::size_t new_vertices = 0;

    if (!g.adjacency.contains(from)) {
        ++new_vertices;
    }

    if (!g.adjacency.contains(to)) {
        ++new_vertices;
    }

    if (new_vertices > 0 &&
        g.adjacency.size() + new_vertices > ResourceLimits::max_graph_vertices) {
        throw RuntimeError{error_msg("Graph", function_name, "graph exceeds maximum vertex count"),
                           loc,
                           std::format("the maximum number of vertices is {}",
                                       ResourceLimits::max_graph_vertices)};
    }

    g.adjacency[from];
    g.adjacency[to];

    if (g.logical_edge_count() + 1 > ResourceLimits::max_graph_edges) {
        throw RuntimeError{
            error_msg("Graph", function_name, "graph exceeds maximum edge count"), loc,
            std::format("the maximum number of edges is {}", ResourceLimits::max_graph_edges)};
    }

    g.adjacency[from].push_back(GraphEdge{.to = to, .weight = weight});

    if (!g.directed) {
        g.adjacency[to].push_back(GraphEdge{.to = from, .weight = weight});
    }
}

} // namespace

// Note: Graph does not use ContainerModuleBuilder because its data is stored as
// adjacency lists (maps of vertex names to edge lists), not a contiguous
// `std::vector<Value> elements`.  ContainerOps assumes a flat elements vector,
// which does not apply to graph-based containers.  Operations like directed(),
// undirected(), add_vertex(), and add_edge() are implemented with graph-specific
// logic that has no analogue in the generic container abstraction.

static void register_graph_construction(const EnvPtr& env) {
    // Graph.directed() — create a directed graph.
    ModuleBuilder{"Graph", env}
        .func("directed", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            auto g = std::make_shared<GraphValue>();
            g->directed = true;

            return Value{std::move(g)};
        })
        // Graph.undirected() — create an undirected graph.
        .func("undirected", 0)
        .raw_body([](std::span<const Value> /*args*/, SourceLocation /*loc*/) -> Value {
            auto g = std::make_shared<GraphValue>();
            g->directed = false;

            return Value{std::move(g)};
        })
        .func("is_directed", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.is_directed", loc);

            return Value{g->directed};
        })
        .func("add_vertex", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.add_vertex", loc);

            const auto& key = expect_string(args[1], "Graph.add_vertex", loc);

            auto result = g->clone();

            if (!result->adjacency.contains(key)) {
                if (result->adjacency.size() + 1 > ResourceLimits::max_graph_vertices) {
                    throw RuntimeError{
                        error_msg("Graph", "add_vertex", "graph exceeds maximum vertex count"), loc,
                        std::format("the maximum number of vertices is {}",
                                    ResourceLimits::max_graph_vertices)};
                }
            }

            result->adjacency[key]; // create entry if absent.

            return Value{std::move(result)};
        })
        // Graph.add_edge uses expect_min_args (3 required, 4th optional weight).
        //
        // Validation order:
        //   1. Argument count and types (vertices must be strings, weight numeric).
        //   2. Vertex limit — checks that adding new vertices won't exceed
        //      ResourceLimits::max_graph_vertices.
        //   3. Edge limit — counts existing logical edges (halved for undirected)
        //      and checks against ResourceLimits::max_graph_edges.
        //   4. Mutation — adds the edge (and reverse edge for undirected graphs).
        .native("add_edge",
                [](std::span<const Value> args, SourceLocation loc) -> Value {
                    expect_min_args("Graph.add_edge", args, 3, loc);

                    auto g = expect_graph(args[0], "Graph.add_edge", loc);

                    const auto& from = expect_string(args[1], "Graph.add_edge", loc);
                    const auto& to = expect_string(args[2], "Graph.add_edge", loc);

                    double weight = 1.0;

                    if (args.size() >= 4) {
                        weight = expect_numeric(args[3], "Graph.add_edge", loc);

                        // Reject every non-finite weight at the boundary — the only place
                        // a user-supplied weight enters the graph — so the weighted-path
                        // algorithms only ever see finite, orderable values.  NaN is not
                        // comparable, so it breaks the strict weak ordering that
                        // minimum_spanning_tree's edge sort relies on (undefined
                        // behaviour) and poisons dijkstra's `d + weight` relaxation.
                        // ±Infinity is just as unsafe: +Infinity is indistinguishable from
                        // the "unreachable" distance sentinel, and mixing +Infinity with
                        // -Infinity in the Floyd-Warshall relaxation (`dist[i][k] +
                        // dist[k][j]`) produces NaN, reintroducing the same unordered value
                        // that all_pairs_shortest_paths would then store.
                        if (!std::isfinite(weight)) {
                            throw RuntimeError{
                                error_msg("Graph", "add_edge", "edge weight must be finite"), loc,
                                "NaN and ±Infinity cannot be ordered safely, so they would "
                                "break shortest-path and minimum-spanning-tree computations"};
                        }
                    }

                    auto result = g->clone();

                    // Check vertex limit for new vertices.
                    std::size_t new_vertices = 0;

                    if (!result->adjacency.contains(from)) {
                        ++new_vertices;
                    }

                    if (!result->adjacency.contains(to)) {
                        ++new_vertices;
                    }

                    if (new_vertices > 0 && result->adjacency.size() + new_vertices >
                                                ResourceLimits::max_graph_vertices) {
                        throw RuntimeError{
                            error_msg("Graph", "add_edge", "graph exceeds maximum vertex count"),
                            loc,
                            std::format("the maximum number of vertices is {}",
                                        ResourceLimits::max_graph_vertices)};
                    }

                    // Ensure both vertices exist.
                    result->adjacency[from];
                    result->adjacency[to];

                    // Check edge limit (logical edge count).
                    if (result->logical_edge_count() + 1 > ResourceLimits::max_graph_edges) {
                        throw RuntimeError{
                            error_msg("Graph", "add_edge", "graph exceeds maximum edge count"), loc,
                            std::format("the maximum number of edges is {}",
                                        ResourceLimits::max_graph_edges)};
                    }

                    result->adjacency[from].push_back(GraphEdge{.to = to, .weight = weight});

                    if (!result->directed) {
                        result->adjacency[to].push_back(GraphEdge{.to = from, .weight = weight});
                    }

                    return Value{std::move(result)};
                })
        // Graph.from_edges(edges, directed) — build a graph from an array of
        // Graph.Edge records, adding each edge's endpoints as vertices.  The
        // inverse companion of Graph.edges.
        .func("from_edges", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& edges = expect_array(args[0], "Graph.from_edges", loc);
            const bool directed = expect_boolean(args[1], "Graph.from_edges", loc);

            auto g = std::make_shared<GraphValue>();
            g->directed = directed;

            for (const auto& elem : *edges->elements) {
                if (!elem.is_record()) {
                    throw RuntimeError{
                        error_msg("Graph", "from_edges",
                                  "each element must be a Graph.Edge record"),
                        loc, "build edges with Graph.edges or construct Graph.Edge records"};
                }

                const auto& rec = *elem.as_record();
                const auto* from = rec.find_field("from");
                const auto* to = rec.find_field("to");
                const auto* weight = rec.find_field("weight");

                if (from == nullptr || !from->is_string() || to == nullptr || !to->is_string()) {
                    throw RuntimeError{
                        error_msg("Graph", "from_edges",
                                  "each edge must have string 'from' and 'to' fields"),
                        loc};
                }

                const double w =
                    (weight != nullptr && (weight->is_number() || weight->is_integer()))
                        ? weight->to_numeric()
                        : 1.0;

                add_edge_into(*g, from->as_string(), to->as_string(), w, "from_edges", loc);
            }

            return Value{std::move(g)};
        })
        // Graph.from_adjacency_list(adj, directed) — build a graph from a
        // dictionary mapping each vertex to its array of neighbour names.  The
        // inverse companion of Graph.to_adjacency_list.
        .func("from_adjacency_list", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            const auto& adj = expect_dict(args[0], "Graph.from_adjacency_list", loc);
            const bool directed = expect_boolean(args[1], "Graph.from_adjacency_list", loc);

            auto g = std::make_shared<GraphValue>();
            g->directed = directed;

            for (const auto& [vertex, neighbours] : adj->entries) {
                // A vertex may have no outgoing edges; ensure it still exists.
                if (g->adjacency.size() + 1 > ResourceLimits::max_graph_vertices) {
                    throw RuntimeError{error_msg("Graph", "from_adjacency_list",
                                                 "graph exceeds maximum vertex count"),
                                       loc};
                }
                g->adjacency[vertex];

                if (!neighbours.is_array()) {
                    throw RuntimeError{error_msg("Graph", "from_adjacency_list",
                                                 "each value must be an array of neighbour names"),
                                       loc};
                }

                for (const auto& n : *neighbours.as_array()->elements) {
                    if (!n.is_string()) {
                        throw RuntimeError{error_msg("Graph", "from_adjacency_list",
                                                     "neighbour names must be strings"),
                                           loc};
                    }

                    add_edge_into(*g, vertex, n.as_string(), 1.0, "from_adjacency_list", loc);
                }
            }

            return Value{std::move(g)};
        })
        // Graph.reverse(g) — a new directed graph with every edge flipped
        // (weights preserved).  For an undirected graph this returns an
        // equivalent copy, since its edges are already symmetric.
        .func("reverse", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.reverse", loc);

            if (!g->directed) {
                return Value{g->clone()};
            }

            auto result = std::make_shared<GraphValue>();
            result->directed = true;

            // Preserve isolated vertices (those with no outgoing edges).
            for (const auto& [vertex, _] : g->adjacency) {
                result->adjacency[vertex];
            }

            for (const auto& [from, edges] : g->adjacency) {
                for (const auto& e : edges) {
                    result->adjacency[e.to].push_back(GraphEdge{.to = from, .weight = e.weight});
                }
            }

            return Value{std::move(result)};
        });
}

// Membership, adjacency, and structural queries over an existing graph.
static void register_graph_queries(const EnvPtr& env) {
    ModuleBuilder{"Graph", env}
        .func("has_vertex", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.has_vertex", loc);

            const auto& key = expect_string(args[1], "Graph.has_vertex", loc);

            return Value{g->adjacency.contains(key)};
        })
        .func("has_edge", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.has_edge", loc);

            const auto& from = expect_string(args[1], "Graph.has_edge", loc);
            const auto& to = expect_string(args[2], "Graph.has_edge", loc);

            auto it = g->adjacency.find(from);

            if (it == g->adjacency.end()) {
                return Value{false};
            }

            return Value{find_edge_to(it->second, to) != it->second.end()};
        })
        .func("vertices", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.vertices", loc);
            auto arr = std::make_shared<ArrayValue>();

            for (const auto& [key, _] : g->adjacency) {
                arr->elements->emplace_back(key);
            }

            return Value{std::move(arr)};
        })
        .func("neighbors", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.neighbors", loc);

            const auto& key = expect_string(args[1], "Graph.neighbors", loc);

            auto it = g->adjacency.find(key);

            if (it == g->adjacency.end()) {
                return make_failure_value(std::string{"vertex not found"});
            }

            auto arr = std::make_shared<ArrayValue>();

            // Cap the result at max_array_size: an undirected self-loop pushes two
            // physical adjacency entries per logical edge (logical_edge_count halves
            // the total), so a vertex's list can reach ~2x max_graph_edges while the
            // add_edge cap passes, which would otherwise build an over-limit array.
            for (const auto& e : it->second) {
                validate_container_size(arr->elements->size(), ResourceLimits::max_array_size,
                                        "Graph.neighbors", loc);
                arr->elements->emplace_back(e.to);
            }

            return make_success_value(Value{std::move(arr)});
        })
        .func("vertex_count", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.vertex_count", loc);

            return Value{static_cast<std::int64_t>(g->adjacency.size())};
        })
        .func("edge_count", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.edge_count", loc);

            return Value{static_cast<std::int64_t>(g->logical_edge_count())};
        })
        .func("edges", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.edges", loc);

            // Enumerate vertices in sorted order — the adjacency map is unordered,
            // so a raw traversal would vary across runs and standard libraries.
            std::vector<std::string> names;
            names.reserve(g->adjacency.size());

            for (const auto& [name, _] : g->adjacency) {
                names.push_back(name);
            }

            std::ranges::sort(names);

            auto arr = std::make_shared<ArrayValue>();

            const auto emit = [&](const std::string& from, const std::string& to, double weight) {
                validate_container_size(arr->elements->size(), ResourceLimits::max_array_size,
                                        "Graph.edges", loc);

                auto rec = std::make_shared<RecordValue>();
                rec->type_name = "Edge";
                rec->fields.emplace_back("from", Value{from});
                rec->fields.emplace_back("to", Value{to});
                rec->fields.emplace_back("weight", Value{weight});

                arr->elements->emplace_back(Value{std::move(rec)});
            };

            for (const auto& from : names) {
                // Copy and sort each vertex's edges by (to, weight) so the output
                // order is fully determined by the graph's contents.
                std::vector<GraphEdge> edges = g->adjacency.at(from);
                std::ranges::sort(edges, [](const GraphEdge& a, const GraphEdge& b) {
                    return a.to != b.to ? a.to < b.to : a.weight < b.weight;
                });

                if (g->directed) {
                    for (const auto& e : edges) {
                        emit(from, e.to, e.weight);
                    }

                    continue;
                }

                // Undirected: every logical edge is stored twice (once in each
                // endpoint's list), so emit each exactly once — matching
                // edge_count / logical_edge_count.  For an edge between distinct
                // vertices the copy where from < to is the representative; a
                // self-loop is stored twice in the same list, so emit every second.
                std::size_t self_loops_seen = 0;

                for (const auto& e : edges) {
                    if (from < e.to) {
                        emit(from, e.to, e.weight);
                    } else if (from == e.to) {
                        if (self_loops_seen % 2 == 0) {
                            emit(from, e.to, e.weight);
                        }

                        ++self_loops_seen;
                    }
                    // from > e.to: the representative was emitted while processing e.to.
                }
            }

            return Value{std::move(arr)};
        })
        .func("degree", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.degree", loc);

            const auto& key = expect_string(args[1], "Graph.degree", loc);

            auto it = g->adjacency.find(key);

            if (it == g->adjacency.end()) {
                return make_failure_value(std::string{"vertex not found"});
            }

            return make_success_value(Value{static_cast<std::int64_t>(it->second.size())});
        })
        .func("out_degree", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.out_degree", loc);

            const auto& key = expect_string(args[1], "Graph.out_degree", loc);

            auto it = g->adjacency.find(key);

            if (it == g->adjacency.end()) {
                return make_failure_value(std::string{"vertex not found"});
            }

            // Outgoing edges are exactly the vertex's own adjacency list.
            return make_success_value(Value{static_cast<std::int64_t>(it->second.size())});
        })
        .func("in_degree", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.in_degree", loc);

            const auto& key = expect_string(args[1], "Graph.in_degree", loc);

            if (!g->adjacency.contains(key)) {
                return make_failure_value(std::string{"vertex not found"});
            }

            // Count every edge across the graph that targets `key`.  For an
            // undirected graph edges are stored symmetrically, so this equals the
            // vertex's degree.
            std::int64_t count = 0;

            for (const auto& [_, edges] : g->adjacency) {
                for (const auto& e : edges) {
                    if (e.to == key) {
                        ++count;
                    }
                }
            }

            return make_success_value(Value{count});
        })
        .func("predecessors", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.predecessors", loc);

            const auto& key = expect_string(args[1], "Graph.predecessors", loc);

            if (!g->adjacency.contains(key)) {
                return make_failure_value(std::string{"vertex not found"});
            }

            // The reverse of neighbors: every vertex with an edge into `key`.
            // The adjacency map is unordered, so sort for deterministic output.
            std::vector<std::string> preds;

            for (const auto& [vertex, edges] : g->adjacency) {
                const bool points_to_key =
                    std::ranges::any_of(edges, [&](const GraphEdge& e) { return e.to == key; });
                if (points_to_key) {
                    preds.push_back(vertex);
                }
            }

            std::ranges::sort(preds);

            auto arr = std::make_shared<ArrayValue>();

            for (const auto& p : preds) {
                arr->elements->emplace_back(p);
            }

            return make_success_value(Value{std::move(arr)});
        })
        .func("density", 1)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.density", loc);

            const auto vertices = g->adjacency.size();

            // Density is undefined (conventionally 0) for a graph with fewer than
            // two vertices — there are no possible edges.
            if (vertices < 2) {
                return Value{0.0};
            }

            const auto edges = static_cast<double>(g->logical_edge_count());
            const auto max_directed =
                static_cast<double>(vertices) * static_cast<double>(vertices - 1);

            // Directed: E / (V*(V-1)).  Undirected: 2E / (V*(V-1)), since each of
            // the E logical edges could exist between an unordered pair.
            const double density =
                g->directed ? edges / max_directed : (2.0 * edges) / max_directed;

            return Value{density};
        })
        .func("remove_vertex", 2)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.remove_vertex", loc);

            const auto& key = expect_string(args[1], "Graph.remove_vertex", loc);

            auto result = std::make_shared<GraphValue>();
            result->directed = g->directed;

            for (const auto& [v, edges] : g->adjacency) {
                if (v == key) {
                    continue;
                }

                auto& new_edges = result->adjacency[v];

                for (const auto& e : edges) {
                    if (e.to != key) {
                        new_edges.push_back(e);
                    }
                }
            }

            return Value{std::move(result)};
        })
        .func("remove_edge", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.remove_edge", loc);

            const auto& from = expect_string(args[1], "Graph.remove_edge", loc);
            const auto& to = expect_string(args[2], "Graph.remove_edge", loc);

            auto result = g->clone();

            // Remove first matching edge from -> to.
            auto it = result->adjacency.find(from);

            if (it != result->adjacency.end()) {
                auto& edges = it->second;
                auto pos = find_edge_to(edges, to);

                if (pos != edges.end()) {
                    edges.erase(pos);
                }
            }

            if (!result->directed) {
                auto it2 = result->adjacency.find(to);

                if (it2 != result->adjacency.end()) {
                    auto& edges = it2->second;
                    auto pos = find_edge_to(edges, from);

                    if (pos != edges.end()) {
                        edges.erase(pos);
                    }
                }
            }

            return Value{std::move(result)};
        })
        .func("edge_weight", 3)
        .raw_body([](std::span<const Value> args, SourceLocation loc) -> Value {
            auto g = expect_graph(args[0], "Graph.edge_weight", loc);

            const auto& from = expect_string(args[1], "Graph.edge_weight", loc);
            const auto& to = expect_string(args[2], "Graph.edge_weight", loc);

            auto it = g->adjacency.find(from);

            if (it != g->adjacency.end()) {
                auto pos = find_edge_to(it->second, to);

                if (pos != it->second.end()) {
                    return make_success_value(Value{pos->weight});
                }
            }

            return make_failure_value(std::string{"edge not found"});
        });
}

// Graph namespace: wires up construction, query, and algorithm operations.
void register_graph_ns(const EnvPtr& env) {
    register_graph_construction(env);
    register_graph_queries(env);
    register_graph_algorithms(env);
}
} // namespace luma
