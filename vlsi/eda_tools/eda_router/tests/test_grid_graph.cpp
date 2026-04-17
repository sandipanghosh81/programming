// ═══════════════════════════════════════════════════════════════════════════════
// FILE: test_grid_graph.cpp  —  Tests for RoutingGridGraph (Boost.Graph lattice)
// ═══════════════════════════════════════════════════════════════════════════════
//
// WHAT WE ARE TESTING:
//   Every public method of RoutingGridGraph — construction, vertex lookup,
//   edge existence, edge claims, rip-up, freezing, overflow calculation.
//
// HOW TO RUN:
//   cd build && ctest -V -R test_grid_graph
//   OR: ./tests/test_grid_graph
//
// ─── TESTING FRAMEWORK: Simple assert-based (no external framework required) ──
// We use a lightweight ASSERT macro that prints a clear message on failure
// including the file, line, and condition text.
// ─────────────────────────────────────────────────────────────────────────────
#include <cassert>
#include <iostream>
#include <vector>
#include "routing_genetic_astar/grid_graph.hpp"

using namespace routing_genetic_astar;

// ─── Test helper macro ────────────────────────────────────────────────────────
// ANALOGY: A quality-control checklist.  Each CHECK_TRUE item is one checkbox.
// If any box fails, the checklist stops and reports WHERE it failed.
#define CHECK_TRUE(condition, msg) \
    do { \
        if (!(condition)) { \
            std::cerr << "FAIL: " << msg << "\n"; \
            std::cerr << "  at " << __FILE__ << ":" << __LINE__ << "\n"; \
            return false; \
        } \
        std::cout << "  PASS: " << msg << "\n"; \
    } while(false)

// ─────────────────────────────────────────────────────────────────────────────
// TEST 1: build_lattice — correct vertex and edge counts
//
// For a 3×3 grid with 2 layers:
//   Vertices: 3×3×2 = 18
//   Horizontal edges: 2 × 3 × 2 = 12  (2 edges per row, 3 rows, 2 layers)
//   Vertical edges:   2 × 2 × 3 = 12  (2 rows of edges per layer, 3 cols, 2 layers)
//   Via edges:        3 × 3 × 1 = 9   (1 layer pair × 9 positions)
//   Total edges:      33
// ─────────────────────────────────────────────────────────────────────────────
bool test_vertex_and_edge_counts() {
    std::cout << "\n[TEST 1] build_lattice: vertex and edge counts\n";

    RoutingGridGraph grid;
    std::vector<LayerConfig> layers(2);
    layers[0] = {1.0f, 2.0f};
    layers[1] = {1.0f, 2.0f};
    grid.build_lattice(3, 3, 2, layers);

    const size_t expected_vertices = 3 * 3 * 2;  // = 18
    const size_t expected_h_edges  = 2 * 3 * 2;  // = 12
    const size_t expected_v_edges  = 2 * 2 * 3;  // = 12  (NOTE: 2 layer×, 2 row edges×, 3 cols)
    // Actually: for each layer, for y in [0,rows-2]=[0,1], for x in [0,cols-1]=[0,1,2]
    // = 2 layers × 2 y-rows × 3 x-cols = 12
    const size_t expected_via_edges = 1 * 3 * 3; // = 9

    const size_t actual_vertices = boost::num_vertices(grid.graph());
    const size_t actual_edges    = boost::num_edges(grid.graph());
    const size_t expected_edges  = expected_h_edges + expected_v_edges + expected_via_edges;

    CHECK_TRUE(actual_vertices == expected_vertices,
               "3×3×2 grid has 18 vertices");
    CHECK_TRUE(actual_edges == expected_edges,
               "3×3×2 grid has 33 edges (12 horizontal + 12 vertical + 9 via)");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 2: vertex_at — O(1) coordinate lookup
// ─────────────────────────────────────────────────────────────────────────────
bool test_vertex_at_lookup() {
    std::cout << "\n[TEST 2] vertex_at: coordinate lookup\n";

    RoutingGridGraph grid;
    std::vector<LayerConfig> layers(2, {1.0f, 2.0f});
    grid.build_lattice(4, 5, 2, layers);  // rows=4, cols=5, layers=2

    // Look up corner vertices and check their stored positions.
    VertexDesc v00 = grid.vertex_at(0, 0, 0);
    VertexDesc v42 = grid.vertex_at(4, 3, 1); // x=4, y=3, layer=1 (max corner)

    const auto& pos00 = grid.graph()[v00].pos;
    const auto& pos42 = grid.graph()[v42].pos;

    CHECK_TRUE(pos00.x == 0 && pos00.y == 0 && pos00.z == 0,
               "vertex_at(0,0,0) has correct position");
    CHECK_TRUE(pos42.x == 4 && pos42.y == 3 && pos42.z == 1,
               "vertex_at(4,3,1) has correct position");
    CHECK_TRUE(grid.in_bounds(0, 0, 0), "origin is in bounds");
    CHECK_TRUE(!grid.in_bounds(5, 0, 0), "x=5 is out of bounds for cols=5");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 3: edge_between — optional edge lookup
// ─────────────────────────────────────────────────────────────────────────────
bool test_edge_between() {
    std::cout << "\n[TEST 3] edge_between: std::optional edge lookup\n";

    RoutingGridGraph grid;
    std::vector<LayerConfig> layers(2, {1.0f, 2.0f});
    grid.build_lattice(3, 3, 2, layers);

    VertexDesc v00 = grid.vertex_at(0, 0, 0);
    VertexDesc v10 = grid.vertex_at(1, 0, 0);  // adjacent: horizontal edge exists
    VertexDesc v20 = grid.vertex_at(2, 0, 0);  // not adjacent to v00 (2 steps away)

    auto edge_exists     = grid.edge_between(v00, v10);
    auto edge_not_exists = grid.edge_between(v00, v20);

    // std::optional has operator bool(): true if holds a value, false if empty (nullopt).
    CHECK_TRUE(edge_exists.has_value(), "edge between (0,0,0)→(1,0,0) exists (adjacent)");
    CHECK_TRUE(!edge_not_exists.has_value(), "no direct edge between (0,0,0)→(2,0,0) (2-hop)");

    // Check w_base was set from LayerConfig.
    if (edge_exists) {
        const float w = grid.graph()[*edge_exists].w_base;
        CHECK_TRUE(w == 1.0f, "horizontal edge has w_base = 1.0 (preferred track weight)");
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 4: try_claim_edge and release_net
// ─────────────────────────────────────────────────────────────────────────────
bool test_claim_and_release() {
    std::cout << "\n[TEST 4] try_claim_edge and release_net\n";

    RoutingGridGraph grid;
    std::vector<LayerConfig> layers(2, {1.0f, 2.0f});
    grid.build_lattice(3, 3, 2, layers);

    VertexDesc v00 = grid.vertex_at(0, 0, 0);
    VertexDesc v10 = grid.vertex_at(1, 0, 0);
    auto opt_edge  = grid.edge_between(v00, v10);
    assert(opt_edge.has_value()); // We know this edge exists from test 3.

    EdgeDesc e = *opt_edge;

    // Initially unclaimed.
    CHECK_TRUE(grid.graph()[e].net_owner == -1, "edge is free initially (owner=-1)");

    // Claim for net_id = 42.
    bool first_claim = grid.try_claim_edge(e, 42);
    CHECK_TRUE(first_claim, "first claim by net 42 succeeds");
    CHECK_TRUE(grid.graph()[e].net_owner == 42, "edge now owned by net 42");

    // Conflict: net 99 tries to claim the same edge.
    bool conflict_claim = grid.try_claim_edge(e, 99);
    CHECK_TRUE(!conflict_claim, "second claim by net 99 fails (conflict detected)");
    CHECK_TRUE(grid.graph()[e].net_owner == 42, "edge still owned by net 42 after conflict");

    // GCell demand check.
    float demand_src = grid.graph()[v00].gcell_demand;
    CHECK_TRUE(demand_src == 1.0f, "gcell demand at source vertex incremented to 1.0");

    // Release net 42 → edge becomes free again.
    grid.release_net(42);
    CHECK_TRUE(grid.graph()[e].net_owner == -1, "edge is free after release_net(42)");
    CHECK_TRUE(grid.graph()[v00].gcell_demand == 0.0f, "gcell demand reset after rip-up");

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 5: freeze_net — ECO locked routes
// ─────────────────────────────────────────────────────────────────────────────
bool test_freeze_net() {
    std::cout << "\n[TEST 5] freeze_net: ECO route locking\n";

    RoutingGridGraph grid;
    std::vector<LayerConfig> layers(2, {1.0f, 2.0f});
    grid.build_lattice(3, 3, 2, layers);

    VertexDesc v00 = grid.vertex_at(0, 0, 0);
    VertexDesc v10 = grid.vertex_at(1, 0, 0);
    EdgeDesc e = *grid.edge_between(v00, v10);

    (void)grid.try_claim_edge(e, 7);
    grid.freeze_net(7);  // Lock net 7's route permanently

    CHECK_TRUE(grid.graph()[e].frozen, "edge is frozen after freeze_net(7)");

    // Attempting to claim a frozen edge fails.
    bool try_claim_frozen = grid.try_claim_edge(e, 99);
    CHECK_TRUE(!try_claim_frozen, "cannot claim a frozen edge");

    // release_net on a frozen net should not clear frozen edges.
    grid.release_net(7);
    CHECK_TRUE(grid.graph()[e].net_owner == 7, "frozen edge retains owner after release_net");
    CHECK_TRUE(grid.graph()[e].frozen,          "frozen edge stays frozen after release_net");

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 6: gcell_overflow and total_overflow
// ─────────────────────────────────────────────────────────────────────────────
bool test_overflow_calculation() {
    std::cout << "\n[TEST 6] gcell_overflow and total_overflow\n";

    RoutingGridGraph grid;
    std::vector<LayerConfig> layers(1, {1.0f, 2.0f});  // 1 layer, capacity=1.0
    grid.build_lattice(2, 2, 1, layers);

    VertexDesc v00 = grid.vertex_at(0, 0, 0);
    // Initial overflow = 0 (no claims yet)
    CHECK_TRUE(grid.gcell_overflow(v00) == 0.0f, "no overflow initially");
    CHECK_TRUE(grid.total_overflow() == 0.0f, "zero total overflow initially");

    // Claim 2 edges touching v00: demand becomes 2.0, capacity = 1.0 → overflow = 1.0
    VertexDesc v10 = grid.vertex_at(1, 0, 0);
    VertexDesc v01 = grid.vertex_at(0, 1, 0);
    if (auto e1 = grid.edge_between(v00, v10)) (void)grid.try_claim_edge(*e1, 1);
    if (auto e2 = grid.edge_between(v00, v01)) (void)grid.try_claim_edge(*e2, 2);

    // gcell_capacity was set from preferred_track_weight = 1.0
    // gcell_demand = 2.0 (two claimed edges touch v00)
    float ov = grid.gcell_overflow(v00);
    CHECK_TRUE(ov == 1.0f, "overflow = demand(2.0) - capacity(1.0) = 1.0 after 2 claims");
    CHECK_TRUE(grid.total_overflow() >= 1.0f, "total overflow is non-zero after over-subscription");

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// MAIN: Run all tests and report pass/fail counts
// ─────────────────────────────────────────────────────────────────────────────
int main() {
    std::cout << "═══════════════════════════════════════════════════════\n";
    std::cout << " TEST SUITE: RoutingGridGraph (grid_graph.hpp)\n";
    std::cout << "═══════════════════════════════════════════════════════\n";

    int passed{0}, failed{0};
    auto run = [&](bool(*test_fn)(), const char* name) {
        if (test_fn()) { ++passed; std::cout << "  ✓ " << name << "\n"; }
        else            { ++failed; std::cout << "  ✗ " << name << " FAILED\n"; }
    };

    run(test_vertex_and_edge_counts,  "vertex and edge counts");
    run(test_vertex_at_lookup,        "vertex_at coordinate lookup");
    run(test_edge_between,            "edge_between optional");
    run(test_claim_and_release,       "try_claim and release_net");
    run(test_freeze_net,              "freeze_net ECO flow");
    run(test_overflow_calculation,    "gcell_overflow calculation");

    std::cout << "\n═══════════════════════════════════════════════════════\n";
    std::cout << " Results: " << passed << " passed, " << failed << " failed.\n";
    std::cout << "═══════════════════════════════════════════════════════\n";
    return (failed == 0) ? 0 : 1;
}
