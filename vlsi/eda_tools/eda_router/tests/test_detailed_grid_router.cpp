// ═══════════════════════════════════════════════════════════════════════════════
// FILE: test_detailed_grid_router.cpp  —  Tests for A* Router
// ═══════════════════════════════════════════════════════════════════════════════
//
// WHAT WE ARE TESTING:
//   - That A* finds a path between two adjacent vertices
//   - That A* finds a path between two far vertices
//   - That A* respects the bbox corridor clamp (does not route outside the box)
//   - That A* respects DRC masks (does not expand through blocked directions)
//   - That A* correctly claims edges in the graph (net_owner is set)
//   - That a path does NOT exist when the route is made fully impossible
//
// HOW A* WORKS (summary for code readers):
//   A* is like a smart taxi driver.  Given a start and end address:
//     1. Start at the source vertex.
//     2. At each step, pick the "most promising" next intersection to visit.
//        "Most promising" = lowest (g_cost + h_cost):
//          g_cost = actual cost paid so far to reach here
//          h_cost = heuristic estimate of cost to reach the destination
//          (we use Manhattan distance as the heuristic — fast and admissible)
//     3. Expand to all neighbors.  Skip neighbors that are:
//          a. Already visited (in closed set)  — avoids infinite loops
//          b. Outside the corridor bbox       — corridor clamp
//          c. DRC-blocked in this direction   — rule enforcement
//          d. Owned by another net            — conflict detection
//     4. When the destination vertex is popped from the priority queue → path found.
//     5. Trace back through parent pointers to recover the path.
// ─────────────────────────────────────────────────────────────────────────────
#include <iostream>
#include <vector>
#include "routing_genetic_astar/core/detailed_grid_router.hpp"
#include "routing_genetic_astar/analysis/pin_access_oracle.hpp"
#include "routing_genetic_astar/grid_graph.hpp"

using namespace routing_genetic_astar;

#define CHECK_TRUE(condition, msg) \
    do { \
        if (!(condition)) { \
            std::cerr << "FAIL: " << msg << "\n  at " << __FILE__ << ":" << __LINE__ << "\n"; \
            return false; \
        } \
        std::cout << "  PASS: " << msg << "\n"; \
    } while(false)

// Helper: build a simple grid and return it.
static RoutingGridGraph make_grid(int rows=10, int cols=10, int layers=2) {
    RoutingGridGraph grid;
    std::vector<LayerConfig> cfg(static_cast<size_t>(layers), {1.0f, 2.0f});
    grid.build_lattice(rows, cols, layers, cfg);
    return grid;
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 1: A* finds path between adjacent vertices
// ─────────────────────────────────────────────────────────────────────────────
bool test_adjacent_route() {
    std::cout << "\n[TEST 1] A* routes adjacent vertices (1-hop)\n";

    auto grid = make_grid();
    PinAccessOracle pao; // Empty PAO (allows all approaches)

    VertexDesc src = grid.vertex_at(0, 0, 0);
    VertexDesc dst = grid.vertex_at(1, 0, 0);

    DetailedGridRouter router;
    auto path = router.route_net(0, 0, 1, src, dst, grid, pao);

    CHECK_TRUE(!path.empty(), "path is non-empty for adjacent vertices");
    CHECK_TRUE(path.front() == src, "path starts at source");
    CHECK_TRUE(path.back() == dst, "path ends at destination");
    CHECK_TRUE(path.size() == 2, "adjacent path has exactly 2 vertices");

    // Edge should now be claimed by net 0.
    auto opt_edge = grid.edge_between(src, dst);
    CHECK_TRUE(opt_edge.has_value(), "edge exists between adjacent vertices");
    CHECK_TRUE(grid.graph()[*opt_edge].net_owner == 0, "edge claimed by net 0 after routing");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 2: A* finds path over longer distance (5-hop Manhattan path)
// ─────────────────────────────────────────────────────────────────────────────
bool test_longer_route() {
    std::cout << "\n[TEST 2] A* routes across 5 columns\n";

    auto grid = make_grid(10, 10, 2);
    PinAccessOracle pao;
    VertexDesc src = grid.vertex_at(0, 0, 0);
    VertexDesc dst = grid.vertex_at(5, 0, 0); // 5 columns away

    DetailedGridRouter router;
    auto path = router.route_net(3, 0, 1, src, dst, grid, pao);

    CHECK_TRUE(!path.empty(), "path found across 5 columns");
    CHECK_TRUE(path.front() == src, "path starts at (0,0,0)");
    CHECK_TRUE(path.back() == dst, "path ends at (5,0,0)");
    // Shortest path should be exactly 6 vertices (0,1,2,3,4,5).
    CHECK_TRUE(path.size() == 6, "shortest 5-hop path has 6 vertices");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 3: Corridor bbox clamp — A* must not route outside the box
// ─────────────────────────────────────────────────────────────────────────────
bool test_corridor_clamp() {
    std::cout << "\n[TEST 3] Corridor bbox clamp prevents out-of-corridor routing\n";

    auto grid = make_grid(10, 10, 2);
    PinAccessOracle pao;

    // Source at (0,0,0), Destination at (0,5,0) — must go North 5 hops.
    // BUT: restrict the corridor to y ∈ [0,3] (LAYER 0 only).
    // The destination y=5 is OUTSIDE the corridor — A* should fail to reach it.
    VertexDesc src = grid.vertex_at(0, 0, 0);
    VertexDesc dst = grid.vertex_at(0, 5, 0);

    BoundingBox tight_corridor;
    tight_corridor.x_min = 0; tight_corridor.x_max = 9;
    tight_corridor.y_min = 0; tight_corridor.y_max = 3;   // dst at y=5 is OUTSIDE
    tight_corridor.layer_min = 0; tight_corridor.layer_max = 0;

    DetailedGridRouter router;
    auto path = router.route_net(1, 0, 1, src, dst, grid, pao,
                                  std::optional<BoundingBox>{tight_corridor});

    // Destination is outside the corridor so A* cannot reach it.
    CHECK_TRUE(path.empty(), "A* returns empty path when destination is outside corridor");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 4: DRC mask blocks expansion
// ─────────────────────────────────────────────────────────────────────────────
bool test_drc_block() {
    std::cout << "\n[TEST 4] DRC mask blocks A* expansion\n";

    auto grid = make_grid(2, 3, 1);  // Tiny grid: 2 rows × 3 cols × 1 layer
    PinAccessOracle pao;

    // We want to go from (0,0,0) to (2,0,0).
    // Block EAST movement on ALL edges between col 0 and col 1.
    VertexDesc v00 = grid.vertex_at(0, 0, 0);
    VertexDesc v10 = grid.vertex_at(1, 0, 0);
    auto opt_e = grid.edge_between(v00, v10);
    if (opt_e) grid.graph()[*opt_e].drc_mask |= MASK_EAST;    // Block eastward move

    // Also block the edge from (0,1,0) → (1,1,0)
    VertexDesc v01 = grid.vertex_at(0, 1, 0);
    VertexDesc v11 = grid.vertex_at(1, 1, 0);
    auto opt_e2 = grid.edge_between(v01, v11);
    if (opt_e2) grid.graph()[*opt_e2].drc_mask |= MASK_EAST;  // Block eastward move

    // Now try to go from (0,0,0) to (2,0,0).
    // All eastward paths from col 0 are blocked.  A* should fail.
    VertexDesc src = grid.vertex_at(0, 0, 0);
    VertexDesc dst = grid.vertex_at(2, 0, 0);
    DetailedGridRouter router;
    auto path = router.route_net(2, 0, 1, src, dst, grid, pao);

    CHECK_TRUE(path.empty(), "A* returns empty when all eastward paths are DRC-blocked");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 5: Via transitions — A* correctly uses via edges to change layers
// ─────────────────────────────────────────────────────────────────────────────
bool test_via_transition() {
    std::cout << "\n[TEST 5] A* uses via edges to route to a different layer\n";

    auto grid = make_grid(5, 5, 2);  // 2 layers
    PinAccessOracle pao;

    // Source on layer 0, destination on layer 1 at same (x,y).
    VertexDesc src = grid.vertex_at(2, 2, 0);
    VertexDesc dst = grid.vertex_at(2, 2, 1);

    DetailedGridRouter router;
    auto path = router.route_net(5, 0, 1, src, dst, grid, pao);

    CHECK_TRUE(!path.empty(), "A* routes between layers using via edge");
    CHECK_TRUE(path.size() == 2, "via route has exactly 2 vertices (src + dst)");
    CHECK_TRUE(path.front() == src, "path starts at layer 0 vertex");
    CHECK_TRUE(path.back() == dst,  "path ends at layer 1 vertex");
    return true;
}

int main() {
    std::cout << "═══════════════════════════════════════════════════════\n";
    std::cout << " TEST SUITE: DetailedGridRouter (detailed_grid_router.hpp)\n";
    std::cout << "═══════════════════════════════════════════════════════\n";

    int passed{0}, failed{0};
    auto run = [&](bool(*fn)(), const char* name) {
        if (fn()) { ++passed; } else { ++failed; std::cout << "  ✗ " << name << " FAILED\n"; }
    };

    run(test_adjacent_route,    "adjacent vertices 1-hop");
    run(test_longer_route,      "5-hop Manhattan path");
    run(test_corridor_clamp,    "corridor bbox clamp");
    run(test_drc_block,         "DRC mask blocks expansion");
    run(test_via_transition,    "via layer transition");

    std::cout << "\n Results: " << passed << " passed, " << failed << " failed.\n";
    return (failed == 0) ? 0 : 1;
}
