// ═══════════════════════════════════════════════════════════════════════════════
// FILE: test_convergence_monitor.cpp  —  Tests for Convergence + ILP trigger
// ═══════════════════════════════════════════════════════════════════════════════
//
// WHAT WE ARE TESTING:
//   - Convergence detection: is_converged() is true after conflict_count=0
//   - Oscillation detection: is_oscillating() is true after N passes in a narrow band
//   - SubregionDescriptor: isolate_oscillating_region() builds correct bbox from conflicted edges
//   - Ring buffer: only the LAST window_size passes are tracked (old passes dropped)
//
// HOW ConvergenceMonitor WORKS:
//   Think of it as a sports scoreboard operator watching successive game scores.
//   After every NRL pass, the conflict count (score) is recorded.
//   The monitor keeps the LAST N scores in a ring buffer (std::deque).
//   It declares CONVERGENCE when score = 0 (perfect game).
//   It declares OSCILLATION when max_score - min_score < threshold (stuck in place).
//   On oscillation, it hands off to IlpSolver (the tie-breaker referee).
// ─────────────────────────────────────────────────────────────────────────────
#include <iostream>
#include <vector>
#include "routing_genetic_astar/convergence/convergence_monitor.hpp"
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

// ─────────────────────────────────────────────────────────────────────────────
// TEST 1: Convergence when conflict_count reaches 0
// ─────────────────────────────────────────────────────────────────────────────
bool test_convergence_detection() {
    std::cout << "\n[TEST 1] is_converged() triggers on zero conflicts\n";

    RoutingGridGraph grid;
    std::vector<LayerConfig> cfg(1, {1.0f, 2.0f});
    grid.build_lattice(3, 3, 1, cfg);

    ConvergenceMonitor cm;
    cm.window_size = 4;

    // Feed decreasing conflict counts.
    cm.on_iteration(10, 0, {}, grid);
    CHECK_TRUE(!cm.is_converged(), "not converged after 10 conflicts");

    cm.on_iteration(5, 1, {}, grid);
    cm.on_iteration(2, 2, {}, grid);
    cm.on_iteration(0, 3, {}, grid); // Zero conflicts → converged!

    CHECK_TRUE(cm.is_converged(), "converged after conflict_count=0");
    CHECK_TRUE(!cm.is_oscillating(), "not oscillating when converged");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 2: Oscillation detection (conflict count stuck in narrow band)
// ─────────────────────────────────────────────────────────────────────────────
bool test_oscillation_detection() {
    std::cout << "\n[TEST 2] is_oscillating() triggers on narrow conflict band\n";

    RoutingGridGraph grid;
    std::vector<LayerConfig> cfg(1, {1.0f, 2.0f});
    grid.build_lattice(3, 3, 1, cfg);

    ConvergenceMonitor cm;
    cm.window_size           = 4;
    cm.oscillation_threshold = 2.0f; // max-min < 2.0 → oscillating

    // Feed exactly window_size conflict counts all within a 1-unit band → oscillating
    for (int pass = 0; pass < 4; ++pass)
        cm.on_iteration(10 + (pass % 2), pass, {}, grid); // Alternates between 10 and 11

    CHECK_TRUE(!cm.is_converged(), "not converged (conflicts > 0)");
    CHECK_TRUE(cm.is_oscillating(),
               "oscillating after 4 passes with max-min = 1 < threshold 2.0");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 3: Ring buffer — old passes are dropped after window_size
// ─────────────────────────────────────────────────────────────────────────────
bool test_ring_buffer_window() {
    std::cout << "\n[TEST 3] Ring buffer drops old passes after window_size\n";

    RoutingGridGraph grid;
    std::vector<LayerConfig> cfg(1, {1.0f, 2.0f});
    grid.build_lattice(3, 3, 1, cfg);

    ConvergenceMonitor cm;
    cm.window_size           = 4;
    cm.oscillation_threshold = 2.0f;

    // First 4 passes: highly variable (should NOT oscillate)
    cm.on_iteration(100, 0, {}, grid);
    cm.on_iteration(1,   1, {}, grid);
    cm.on_iteration(100, 2, {}, grid);
    cm.on_iteration(1,   3, {}, grid);

    // After 4: max-min = 99, NOT oscillating.
    CHECK_TRUE(!cm.is_oscillating(), "not oscillating with wide conflict range [1,100]");

    // Now feed 4 more narrow passes → the old wide passes are pushed out.
    cm.on_iteration(5, 4, {}, grid);
    cm.on_iteration(5, 5, {}, grid);
    cm.on_iteration(5, 6, {}, grid);
    cm.on_iteration(5, 7, {}, grid);

    CHECK_TRUE(cm.is_oscillating(), "oscillating after window fills with constant 5");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 4: isolate_oscillating_region builds correct bbox from conflicted edges
// ─────────────────────────────────────────────────────────────────────────────
bool test_isolate_region() {
    std::cout << "\n[TEST 4] isolate_oscillating_region builds subregion bbox\n";

    RoutingGridGraph grid;
    std::vector<LayerConfig> cfg(1, {1.0f, 2.0f});
    grid.build_lattice(10, 10, 1, cfg);

    // Claim some edges to create fake conflicts at (2,3) and (4,5).
    VertexDesc v23 = grid.vertex_at(2, 3, 0);
    VertexDesc v33 = grid.vertex_at(3, 3, 0);
    VertexDesc v45 = grid.vertex_at(4, 5, 0);
    VertexDesc v55 = grid.vertex_at(5, 5, 0);

    std::vector<EdgeDesc> conflicts;
    if (auto e1 = grid.edge_between(v23, v33)) {
        (void)grid.try_claim_edge(*e1, 7);
        grid.graph()[*e1].w_cong_history = 1.0f; // mark as conflicted
        conflicts.push_back(*e1);
    }
    if (auto e2 = grid.edge_between(v45, v55)) {
        (void)grid.try_claim_edge(*e2, 8);
        grid.graph()[*e2].w_cong_history = 1.0f;
        conflicts.push_back(*e2);
    }

    ConvergenceMonitor cm;
    cm.window_size           = 4;
    cm.oscillation_threshold = 2.0f;

    // Feed oscillating passes with the conflict edges.
    for (int p = 0; p < 4; ++p)
        cm.on_iteration(5, p, conflicts, grid);

    CHECK_TRUE(cm.is_oscillating(), "oscillating as expected");

    auto subregion = cm.isolate_oscillating_region();
    CHECK_TRUE(subregion.has_value(), "subregion is present when oscillating");

    const auto& bbox = subregion->bbox;
    // The bbox should contain (2,3,0) and (5,5,0) — covers both conflict sources.
    CHECK_TRUE(bbox.x_min <= 2 && bbox.x_max >= 5, "bbox x range covers conflict columns 2..5");
    CHECK_TRUE(bbox.y_min <= 3 && bbox.y_max >= 5, "bbox y range covers conflict rows 3..5");

    // Net IDs 7 and 8 should both appear in the subregion's net list.
    const auto& nids = subregion->net_ids;
    bool has_7 = std::find(nids.begin(), nids.end(), 7) != nids.end();
    bool has_8 = std::find(nids.begin(), nids.end(), 8) != nids.end();
    CHECK_TRUE(has_7, "net 7 is in oscillating subregion");
    CHECK_TRUE(has_8, "net 8 is in oscillating subregion");

    return true;
}

int main() {
    std::cout << "═══════════════════════════════════════════════════════\n";
    std::cout << " TEST SUITE: ConvergenceMonitor (convergence_monitor.hpp)\n";
    std::cout << "═══════════════════════════════════════════════════════\n";

    int passed{0}, failed{0};
    auto run = [&](bool(*fn)(), const char* name) {
        if (fn()) { ++passed; } else { ++failed; std::cout << "  ✗ " << name << " FAILED\n"; }
    };

    run(test_convergence_detection, "convergence on zero conflicts");
    run(test_oscillation_detection, "oscillation on narrow band");
    run(test_ring_buffer_window,    "ring buffer window eviction");
    run(test_isolate_region,        "isolate oscillating subregion bbox");

    std::cout << "\n Results: " << passed << " passed, " << failed << " failed.\n";
    return (failed == 0) ? 0 : 1;
}
