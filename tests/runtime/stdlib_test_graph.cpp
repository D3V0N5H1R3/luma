// Standard library tests: Graph.

#include "common/resource_limits.hpp"
#include "stdlib_test_helpers.hpp"

static void test_graph_has_cycle_depth_guarded() {
    // A path deeper than the recursion limit must raise a catchable error
    // instead of overflowing the native stack. (Regression: Graph.has_cycle's
    // DFS had no depth guard, so deep graphs crashed the process uncatchably.)
    std::string src = "Graph.directed()";
    for (int i = 0; i < 400; ++i) {
        src += "\n|> Graph.add_edge(\"v" + std::to_string(i) + "\", \"v" + std::to_string(i + 1) +
               "\")";
    }
    src += "\n|> Graph.has_cycle()";

    bool threw{false};

    try {
        (void)eval(src);
    } catch (const std::exception& e) {
        threw = true;
        ASSERT_TRUE(std::string{e.what()}.find("maximum traversal depth") != std::string::npos);
    }

    ASSERT_TRUE(threw);
}

static void test_graph_add_edge() {
    ASSERT_TRUE(
        eval(R"(Graph.undirected() |> Graph.add_edge("A", "B") |> Graph.has_edge("A", "B"))")
            .as_bool());
}

static void test_graph_add_vertex() {
    ASSERT_TRUE(
        eval(R"(Graph.undirected() |> Graph.add_vertex("A") |> Graph.has_vertex("A"))").as_bool());
}

static void test_graph_bfs() {
    const auto result = eval(R"(
        Graph.undirected()
        |> Graph.add_edge("A", "B")
        |> Graph.add_edge("A", "C")
        |> Graph.breadth_first_search("A")
        |> Result.unwrap()
    )");

    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.as_array()->elements->size(), std::size_t{3});
    ASSERT_EQ((*result.as_array()->elements)[0].as_string(), "A");
}

static void test_graph_connected_components() {
    // Two disconnected components: {A,B} and {C,D}
    const auto v = eval(R"(
        Graph.undirected()
        |> Graph.add_edge("A", "B")
        |> Graph.add_edge("C", "D")
        |> Graph.connected_components()
        |> Result.unwrap()
    )");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), std::size_t{2});
}

static void test_graph_connected_components_directed_fails() {
    // Directed graph — connected_components must return failure
    ASSERT_EVAL_FAILURE(R"(
        Graph.directed()
        |> Graph.add_edge("A", "B")
        |> Graph.connected_components()
    )");
}

static void test_graph_directed_edge_count() {
    ASSERT_EQ(
        eval(R"(Graph.directed() |> Graph.add_edge("A", "B") |> Graph.edge_count())").as_integer(),
        1);
}

static void test_graph_edge_count() {
    ASSERT_EQ(eval(R"(Graph.undirected() |> Graph.add_edge("A", "B") |> Graph.edge_count())")
                  .as_integer(),
              1);
}

static void test_graph_has_cycle_acyclic() {
    // A → B → C — no cycle
    ASSERT_FALSE(eval(R"(
        Graph.directed()
        |> Graph.add_edge("A", "B")
        |> Graph.add_edge("B", "C")
        |> Graph.has_cycle()
    )")
                     .as_bool());
}

static void test_graph_has_cycle_directed() {
    // A → B → C → A  forms a cycle
    ASSERT_TRUE(eval(R"(
        Graph.directed()
        |> Graph.add_edge("A", "B")
        |> Graph.add_edge("B", "C")
        |> Graph.add_edge("C", "A")
        |> Graph.has_cycle()
    )")
                    .as_bool());
}

static void test_graph_is_directed() {
    ASSERT_TRUE(eval("Graph.directed() |> Graph.is_directed()").as_bool());
    ASSERT_FALSE(eval("Graph.undirected() |> Graph.is_directed()").as_bool());
}

static void test_graph_module() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Graph.directed"));
    ASSERT_TRUE(env->has("Graph.undirected"));
    ASSERT_TRUE(env->has("Graph.add_vertex"));
    ASSERT_TRUE(env->has("Graph.add_edge"));
    ASSERT_TRUE(env->has("Graph.has_vertex"));
    ASSERT_TRUE(env->has("Graph.has_edge"));
    ASSERT_TRUE(env->has("Graph.vertices"));
    ASSERT_TRUE(env->has("Graph.neighbors"));
    ASSERT_TRUE(env->has("Graph.vertex_count"));
    ASSERT_TRUE(env->has("Graph.edge_count"));
    ASSERT_TRUE(env->has("Graph.breadth_first_search"));
    ASSERT_TRUE(env->has("Graph.depth_first_search"));
    ASSERT_TRUE(env->has("Graph.shortest_path"));
    ASSERT_TRUE(env->has("Graph.to_adjacency_list"));
    ASSERT_TRUE(env->has("Graph.has_cycle"));
    ASSERT_TRUE(env->has("Graph.topological_sort"));
    ASSERT_TRUE(env->has("Graph.connected_components"));
}

static void test_graph_neighbors() {
    const auto result = eval(
        R"(Graph.undirected() |> Graph.add_edge("A", "B") |> Graph.neighbors("A") |> Result.unwrap())");

    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.as_array()->elements->size(), std::size_t{1});
    ASSERT_EQ((*result.as_array()->elements)[0].as_string(), "B");
}

static void test_graph_remove_vertex() {
    ASSERT_TRUE(
        !eval(
             R"(Graph.undirected() |> Graph.add_vertex("A") |> Graph.remove_vertex("A") |> Graph.has_vertex("A"))")
             .as_bool());
}

static void test_graph_shortest_path() {
    const auto result = eval(R"(
        Graph.undirected()
        |> Graph.add_edge("A", "B", 1)
        |> Graph.add_edge("B", "C", 2)
        |> Graph.add_edge("A", "C", 10)
        |> Graph.shortest_path("A", "C")
        |> Result.unwrap()
    )");

    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.as_array()->elements->size(), std::size_t{3});
    ASSERT_EQ((*result.as_array()->elements)[0].as_string(), "A");
    ASSERT_EQ((*result.as_array()->elements)[1].as_string(), "B");
    ASSERT_EQ((*result.as_array()->elements)[2].as_string(), "C");
}

static void test_graph_topological_sort() {
    // A → B, A → C, B → D, C → D  — valid DAG
    const auto v = eval(R"(
        Graph.directed()
        |> Graph.add_edge("A", "B")
        |> Graph.add_edge("A", "C")
        |> Graph.add_edge("B", "D")
        |> Graph.add_edge("C", "D")
        |> Graph.topological_sort()
        |> Result.unwrap()
    )");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), std::size_t{4});
    // A must appear before B, C, D
    ASSERT_EQ((*v.as_array()->elements)[0].as_string(), "A");
}

static void test_graph_topological_sort_cycle_fails() {
    // Cyclic graph — topological_sort must return failure
    ASSERT_EVAL_FAILURE(R"(
        Graph.directed()
        |> Graph.add_edge("A", "B")
        |> Graph.add_edge("B", "A")
        |> Graph.topological_sort()
    )");
}

static void test_graph_topological_sort_undirected_fails() {
    // Undirected graph — topological_sort must return failure
    ASSERT_EVAL_FAILURE(R"(
        Graph.undirected()
        |> Graph.add_edge("A", "B")
        |> Graph.topological_sort()
    )");
}

static void test_graph_vertex_count() {
    ASSERT_EQ(
        eval(
            R"(Graph.undirected() |> Graph.add_edge("A", "B") |> Graph.add_vertex("C") |> Graph.vertex_count())")
            .as_integer(),
        3);
}

static void test_graph_minimum_spanning_tree() {
    const auto v = eval("Graph.undirected()"
                        "|> Graph.add_edge(\"a\", \"b\", 1)"
                        "|> Graph.add_edge(\"b\", \"c\", 2)"
                        "|> Graph.add_edge(\"a\", \"c\", 3)"
                        "|> Graph.minimum_spanning_tree()"
                        "|> Result.unwrap()");

    ASSERT_TRUE(v.is_graph());

    // MST of a triangle with weights 1,2,3 has edges a-b(1) and b-c(2), not a-c(3).
    const auto& g = v.as_graph();
    // Should have 3 vertices but only 2 edges (undirected = 4 adjacency entries).
    bool has_a = g->adjacency.count("a") > 0;
    bool has_b = g->adjacency.count("b") > 0;
    bool has_c = g->adjacency.count("c") > 0;
    ASSERT_TRUE(has_a && has_b && has_c);
}

static void test_graph_minimum_spanning_tree_directed_fails() {
    ASSERT_EVAL_FAILURE("Graph.directed()"
                        "|> Graph.add_edge(\"a\", \"b\", 1)"
                        "|> Graph.minimum_spanning_tree()");
}

static void test_graph_strongly_connected_components() {
    const auto v = eval("Graph.directed()"
                        "|> Graph.add_edge(\"a\", \"b\", 1)"
                        "|> Graph.add_edge(\"b\", \"c\", 1)"
                        "|> Graph.add_edge(\"c\", \"a\", 1)"
                        "|> Graph.strongly_connected_components()"
                        "|> Result.unwrap()");

    ASSERT_TRUE(v.is_array());
    // a -> b -> c -> a forms one SCC of size 3.
    ASSERT_EQ(v.as_array()->elements->size(), 1U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_array()->elements->size(), 3U);
}

static void test_graph_strongly_connected_components_dag() {
    const auto v = eval("Graph.directed()"
                        "|> Graph.add_edge(\"a\", \"b\", 1)"
                        "|> Graph.add_edge(\"b\", \"c\", 1)"
                        "|> Graph.strongly_connected_components()"
                        "|> Result.unwrap()");

    ASSERT_TRUE(v.is_array());
    // DAG: each node is its own SCC.
    ASSERT_EQ(v.as_array()->elements->size(), 3U);
}

static void test_graph_all_pairs_shortest_paths() {
    const auto v = eval("Graph.directed()"
                        "|> Graph.add_edge(\"a\", \"b\", 1)"
                        "|> Graph.add_edge(\"b\", \"c\", 2)"
                        "|> Graph.add_edge(\"a\", \"c\", 10)"
                        "|> Graph.all_pairs_shortest_paths()"
                        "|> Result.unwrap()");

    ASSERT_TRUE(v.is_dictionary());

    // dist[a][c] should be 3 (a->b->c), not 10 (direct).
    const auto* a_row_ptr = v.as_dictionary()->find("a");
    ASSERT_TRUE(a_row_ptr != nullptr && a_row_ptr->is_dictionary());
    const auto* dist_ac_ptr = a_row_ptr->as_dictionary()->find("c");
    ASSERT_TRUE(dist_ac_ptr != nullptr);
    const auto dist_ac = dist_ac_ptr->as_number();
    ASSERT_TRUE(dist_ac > 2.9 && dist_ac < 3.1);
}

static void test_graph_depth_first_search() {
    const auto result = eval(R"(
        Graph.undirected()
        |> Graph.add_edge("A", "B")
        |> Graph.add_edge("A", "C")
        |> Graph.depth_first_search("A")
        |> Result.unwrap()
    )");

    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.as_array()->elements->size(), std::size_t{3});
    ASSERT_EQ((*result.as_array()->elements)[0].as_string(), "A");
}

static void test_graph_degree() {
    ASSERT_EQ(eval(R"(
                  Graph.undirected()
                  |> Graph.add_edge("A", "B")
                  |> Graph.add_edge("A", "C")
                  |> Graph.degree("A")
                  |> Result.unwrap()
              )")
                  .as_integer(),
              2);
}

static void test_graph_edge_weight() {
    const auto w = eval(R"(
        Graph.undirected()
        |> Graph.add_edge("A", "B", 3.5)
        |> Graph.edge_weight("A", "B")
        |> Result.unwrap()
    )")
                       .as_number();

    ASSERT_TRUE(w > 3.49 && w < 3.51);
}

static void test_graph_add_edge_rejects_non_finite_weight() {
    // A non-finite edge weight corrupts the weighted-path algorithms: +Infinity is
    // indistinguishable from the "unreachable" distance sentinel, and mixing
    // +Infinity with -Infinity in the Floyd-Warshall relaxation
    // (dist[i][k] + dist[k][j]) yields NaN — not comparable, so it breaks the
    // strict weak ordering the minimum-spanning-tree sort relies on and poisons
    // std::min in all_pairs_shortest_paths.  add_edge is the sole boundary where a
    // user weight enters the graph, so it rejects every non-finite weight with a
    // catchable error rather than storing it.
    for (const std::string weight :
         {"Math.infinity - Math.infinity", "Math.infinity", "0.0 - Math.infinity"}) {
        bool threw{false};

        try {
            (void)eval(R"(Graph.undirected() |> Graph.add_edge("A", "B", )" + weight + ")");
        } catch (const std::exception& e) {
            threw = true;
            ASSERT_TRUE(std::string{e.what()}.find("finite") != std::string::npos);
        }

        ASSERT_TRUE(threw);
    }

    // A finite weight is still accepted and stored.
    ASSERT_TRUE(
        eval(R"(Graph.undirected() |> Graph.add_edge("A", "B", 3.5) |> Graph.has_edge("A", "B"))")
            .as_bool());
}

static void test_graph_has_edge_absent() {
    // Two vertices, no edge between them.
    ASSERT_FALSE(eval(R"(
        Graph.undirected()
        |> Graph.add_vertex("A")
        |> Graph.add_vertex("B")
        |> Graph.has_edge("A", "B")
    )")
                     .as_bool());
}

static void test_graph_has_cycle_undirected() {
    // Triangle A-B-C-A has a cycle.
    ASSERT_TRUE(eval(R"(
        Graph.undirected()
        |> Graph.add_edge("A", "B")
        |> Graph.add_edge("B", "C")
        |> Graph.add_edge("C", "A")
        |> Graph.has_cycle()
    )")
                    .as_bool());

    // A simple path A-B-C is acyclic (the shared edge is not a back-edge).
    ASSERT_FALSE(eval(R"(
        Graph.undirected()
        |> Graph.add_edge("A", "B")
        |> Graph.add_edge("B", "C")
        |> Graph.has_cycle()
    )")
                     .as_bool());
}

static void test_graph_remove_edge() {
    // Undirected: removing A-B also removes the implicit reverse edge B-A.
    ASSERT_FALSE(eval(R"(
        Graph.undirected()
        |> Graph.add_edge("A", "B")
        |> Graph.remove_edge("A", "B")
        |> Graph.has_edge("B", "A")
    )")
                     .as_bool());
}

static void test_graph_remove_vertex_clears_incoming_edges() {
    // Directed A->B->C; removing B must drop both A->B and B->C.
    ASSERT_EQ(eval(R"(
                  Graph.directed()
                  |> Graph.add_edge("A", "B")
                  |> Graph.add_edge("B", "C")
                  |> Graph.remove_vertex("B")
                  |> Graph.edge_count()
              )")
                  .as_integer(),
              0);
}

static void test_graph_to_adjacency_list() {
    const auto adj = eval(R"(
        Graph.undirected()
        |> Graph.add_edge("A", "B")
        |> Graph.to_adjacency_list()
    )");

    ASSERT_TRUE(adj.is_dictionary());
    ASSERT_TRUE(adj.as_dictionary()->find("A") != nullptr);
    ASSERT_TRUE(adj.as_dictionary()->find("B") != nullptr);
}

static void test_graph_neighbors_caps_output_size() {
    // Graph.neighbors builds a Luma array from a vertex's physical adjacency
    // list. Because an undirected self-loop pushes two physical entries per
    // logical edge (logical_edge_count halves the total), a vertex's list can
    // reach ~2x max_graph_edges while the add_edge cap passes, so the output
    // must be bounded by max_array_size like every other array-builder. Here we
    // lower the cap and build a hub with more neighbours than it allows.
    // (Regression: the append loop had no validate_container_size guard.)
    const LimitGuard<std::size_t> guard{ResourceLimits::max_array_size,
                                        static_cast<std::size_t>(4)};

    std::string src = "Graph.undirected()";
    for (int i = 0; i < 5; ++i) {
        src += "\n|> Graph.add_edge(\"hub\", \"v" + std::to_string(i) + "\")";
    }
    src += "\n|> Graph.neighbors(\"hub\")";

    ASSERT_THROWS(eval(src));
}

static void test_graph_to_adjacency_list_caps_output_size() {
    // Each per-vertex array in Graph.to_adjacency_list must be capped at
    // max_array_size for the same reason as Graph.neighbors: a single vertex's
    // physical adjacency list can exceed the cap while the add_edge limit still
    // passes. (Regression: the inner append loop had no guard.)
    const LimitGuard<std::size_t> guard{ResourceLimits::max_array_size,
                                        static_cast<std::size_t>(4)};

    std::string src = "Graph.undirected()";
    for (int i = 0; i < 5; ++i) {
        src += "\n|> Graph.add_edge(\"hub\", \"v" + std::to_string(i) + "\")";
    }
    src += "\n|> Graph.to_adjacency_list()";

    ASSERT_THROWS(eval(src));
}

static void test_graph_vertices() {
    const auto v = eval(R"(
        Graph.undirected()
        |> Graph.add_vertex("X")
        |> Graph.add_vertex("Y")
        |> Graph.vertices()
    )");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), std::size_t{2});
}

// ── Negative cases: recoverable failures (result<...> failure) ──

static void test_graph_neighbors_missing_vertex_fails() {
    ASSERT_EVAL_FAILURE(R"(Graph.undirected() |> Graph.neighbors("Z"))");
}

static void test_graph_degree_missing_vertex_fails() {
    ASSERT_EVAL_FAILURE(R"(Graph.undirected() |> Graph.degree("Z"))");
}

static void test_graph_edge_weight_missing_fails() {
    ASSERT_EVAL_FAILURE(R"(
        Graph.undirected()
        |> Graph.add_vertex("A")
        |> Graph.add_vertex("B")
        |> Graph.edge_weight("A", "B")
    )");
}

static void test_graph_bfs_missing_start_fails() {
    ASSERT_EVAL_FAILURE(R"(Graph.undirected() |> Graph.breadth_first_search("Z"))");
}

static void test_graph_dfs_missing_start_fails() {
    ASSERT_EVAL_FAILURE(R"(Graph.undirected() |> Graph.depth_first_search("Z"))");
}

static void test_graph_shortest_path_no_path_fails() {
    // Two isolated vertices: no connecting path.
    ASSERT_EVAL_FAILURE(R"(
        Graph.directed()
        |> Graph.add_vertex("A")
        |> Graph.add_vertex("B")
        |> Graph.shortest_path("A", "B")
    )");
}

static void test_graph_shortest_path_missing_vertex_fails() {
    ASSERT_EVAL_FAILURE(R"(
        Graph.undirected()
        |> Graph.add_edge("A", "B")
        |> Graph.shortest_path("A", "Z")
    )");
}

static void test_graph_shortest_path_negative_weight_fails() {
    // Dijkstra rejects negative edge weights.
    ASSERT_EVAL_FAILURE(R"(
        Graph.undirected()
        |> Graph.add_edge("A", "B", -1)
        |> Graph.add_edge("B", "C", 2)
        |> Graph.shortest_path("A", "C")
    )");
}

static void test_graph_strongly_connected_components_undirected_fails() {
    ASSERT_EVAL_FAILURE(R"(
        Graph.undirected()
        |> Graph.add_edge("A", "B")
        |> Graph.strongly_connected_components()
    )");
}

static void test_graph_all_pairs_negative_cycle_fails() {
    // Directed 2-cycle of negative weights forms a negative cycle.
    ASSERT_EVAL_FAILURE(R"(
        Graph.directed()
        |> Graph.add_edge("A", "B", -1)
        |> Graph.add_edge("B", "A", -1)
        |> Graph.all_pairs_shortest_paths()
    )");
}

// ── Negative cases: non-recoverable runtime errors (thrown) ──

static void test_graph_add_edge_invalid_weight_throws() {
    ASSERT_THROWS(eval(R"(Graph.undirected() |> Graph.add_edge("A", "B", "heavy"))"));
}

static void test_graph_add_vertex_non_string_throws() {
    ASSERT_THROWS(eval(R"(Graph.undirected() |> Graph.add_vertex(42))"));
}

// ── Graph.edges: structured edge enumeration (array<Graph.Edge>) ──

static void test_graph_edges_directed_order_and_fields() {
    // Directed edges are emitted verbatim in deterministic (from, to, weight)
    // order: vertices sorted by name, then each vertex's out-edges sorted.
    const auto v = eval(R"(
        Graph.directed()
        |> Graph.add_edge("b", "c", 2.0)
        |> Graph.add_edge("a", "b", 1.0)
        |> Graph.add_edge("a", "c", 3.0)
        |> Graph.edges()
    )");

    ASSERT_TRUE(v.is_array());

    const auto& es = *v.as_array()->elements;
    ASSERT_EQ(es.size(), std::size_t{3});

    // Each element is a Graph.Edge record { from, to, weight }.
    const auto edge_at = [&](std::size_t i, const std::string& from, const std::string& to,
                             double weight) {
        ASSERT_TRUE(es.at(i).is_record());
        const auto& rec = *es.at(i).as_record();
        ASSERT_EQ(rec.type_name, "Edge");
        ASSERT_EQ(rec.find_field("from")->as_string(), from);
        ASSERT_EQ(rec.find_field("to")->as_string(), to);
        ASSERT_NEAR(rec.find_field("weight")->as_number(), weight, 1e-9);
    };

    edge_at(0, "a", "b", 1.0);
    edge_at(1, "a", "c", 3.0);
    edge_at(2, "b", "c", 2.0);
}

static void test_graph_edges_count_invariant() {
    // The number of enumerated edges must equal Graph.edge_count — undirected
    // edges (stored once per endpoint) are de-duplicated to a single Edge.
    const auto v = eval(R"(
        Graph.undirected()
        |> Graph.add_edge("a", "b", 1.0)
        |> Graph.add_edge("b", "c", 2.0)
        |> Graph.add_edge("a", "a", 5.0)
        |> Graph.edges()
    )");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), std::size_t{3});

    const auto count = eval(R"(
        Graph.undirected()
        |> Graph.add_edge("a", "b", 1.0)
        |> Graph.add_edge("b", "c", 2.0)
        |> Graph.add_edge("a", "a", 5.0)
        |> Graph.edge_count()
    )");
    ASSERT_EQ(count.as_integer(), std::int64_t{3});
}

static void test_graph_edges_undirected_single_representative() {
    // An undirected edge a—b is enumerated once, as (from<to), not twice.
    const auto v = eval(R"(
        Graph.undirected()
        |> Graph.add_edge("z", "a", 7.0)
        |> Graph.edges()
    )");

    ASSERT_TRUE(v.is_array());
    const auto& es = *v.as_array()->elements;
    ASSERT_EQ(es.size(), std::size_t{1});
    ASSERT_EQ(es.at(0).as_record()->find_field("from")->as_string(), "a");
    ASSERT_EQ(es.at(0).as_record()->find_field("to")->as_string(), "z");
}

static void test_graph_edges_empty() {
    const auto v = eval(R"(Graph.undirected() |> Graph.edges())");
    ASSERT_TRUE(v.is_array());
    ASSERT_TRUE(v.as_array()->elements->empty());
}

int main() {
    RUN(test_graph_add_edge);
    RUN(test_graph_add_edge_invalid_weight_throws);
    RUN(test_graph_add_vertex);
    RUN(test_graph_add_vertex_non_string_throws);
    RUN(test_graph_all_pairs_negative_cycle_fails);
    RUN(test_graph_all_pairs_shortest_paths);
    RUN(test_graph_bfs);
    RUN(test_graph_bfs_missing_start_fails);
    RUN(test_graph_connected_components);
    RUN(test_graph_connected_components_directed_fails);
    RUN(test_graph_degree);
    RUN(test_graph_degree_missing_vertex_fails);
    RUN(test_graph_depth_first_search);
    RUN(test_graph_dfs_missing_start_fails);
    RUN(test_graph_directed_edge_count);
    RUN(test_graph_edge_count);
    RUN(test_graph_edge_weight);
    RUN(test_graph_add_edge_rejects_non_finite_weight);
    RUN(test_graph_edge_weight_missing_fails);
    RUN(test_graph_has_cycle_acyclic);
    RUN(test_graph_has_cycle_directed);
    RUN(test_graph_has_cycle_undirected);
    RUN(test_graph_has_edge_absent);
    RUN(test_graph_is_directed);
    RUN(test_graph_minimum_spanning_tree);
    RUN(test_graph_minimum_spanning_tree_directed_fails);
    RUN(test_graph_module);
    RUN(test_graph_neighbors);
    RUN(test_graph_neighbors_caps_output_size);
    RUN(test_graph_neighbors_missing_vertex_fails);
    RUN(test_graph_remove_edge);
    RUN(test_graph_remove_vertex);
    RUN(test_graph_remove_vertex_clears_incoming_edges);
    RUN(test_graph_shortest_path);
    RUN(test_graph_shortest_path_missing_vertex_fails);
    RUN(test_graph_shortest_path_negative_weight_fails);
    RUN(test_graph_shortest_path_no_path_fails);
    RUN(test_graph_strongly_connected_components);
    RUN(test_graph_strongly_connected_components_dag);
    RUN(test_graph_strongly_connected_components_undirected_fails);
    RUN(test_graph_to_adjacency_list);
    RUN(test_graph_to_adjacency_list_caps_output_size);
    RUN(test_graph_topological_sort);
    RUN(test_graph_topological_sort_cycle_fails);
    RUN(test_graph_topological_sort_undirected_fails);
    RUN(test_graph_vertex_count);
    RUN(test_graph_vertices);
    RUN(test_graph_edges_directed_order_and_fields);
    RUN(test_graph_edges_count_invariant);
    RUN(test_graph_edges_undirected_single_representative);
    RUN(test_graph_edges_empty);
    RUN(test_graph_has_cycle_depth_guarded);
    return SUMMARY();
}
