// ═══════════════════════════════════════════════════════════════════════════════
// FILE: test_strategy_composer.cpp  —  Tests for StrategyComposer (std::visit dispatch)
// ═══════════════════════════════════════════════════════════════════════════════
//
// WHAT WE ARE TESTING:
//   That StrategyComposer correctly dispatches to the right router based on context:
//     RandomLogicTag  + 2 pins  → DetailedGridRouter (direct A*)
//     RandomLogicTag  + 3 pins  → TreeRouter (Steiner)
//     ClockNetworkTag + N sinks → SpineFishboneRouter (H-tree)
//     MemoryArrayTag            → Returns empty (GridFill handled it in GlobalPlanner)
//
// ─── C++ FEATURE BEING TESTED: std::visit + if constexpr ─────────────────────
//
// std::visit takes a std::variant and a visitor (lambda or struct with operator())
// and calls the visitor with the CORRECT overload for whatever type is stored.
//
// ANALOGY: A smart vending machine that reads your "product tag" and
// automatically hands you the right item:
//   TAG = Chips  → dispenses chips (no candy, no soda)
//   TAG = Candy  → dispenses candy
//   TAG = Soda   → dispenses soda
//
// `if constexpr (std::is_same_v<T, ClockNetworkTag>)` evaluates at COMPILE TIME:
//   - If T IS ClockNetworkTag, the `if` branch is compiled; the `else` is discarded.
//   - If T is NOT ClockNetworkTag, the `if` branch is DISCARDED at compile time.
//   This is different from a runtime `if`: the WRONG branch code doesn't even exist
//   in the final binary.  Faster and safer.
// ─────────────────────────────────────────────────────────────────────────────
#include <iostream>
#include <vector>
#include "routing_genetic_astar/routing/strategy_composer.hpp"
#include "routing_genetic_astar/analysis/pin_access_oracle.hpp"
#include "routing_genetic_astar/types.hpp"
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

static RoutingGridGraph make_grid(int rows=15, int cols=15, int layers=2) {
    RoutingGridGraph grid;
    std::vector<LayerConfig> cfg(static_cast<size_t>(layers), {1.0f, 2.0f});
    grid.build_lattice(rows, cols, layers, cfg);
    return grid;
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 1: 2-pin random logic → DetailedGridRouter → non-empty path
// ─────────────────────────────────────────────────────────────────────────────
bool test_two_pin_random_logic() {
    std::cout << "\n[TEST 1] RandomLogicTag + 2 pins → DetailedGridRouter (direct A*)\n";

    auto grid = make_grid();
    PinAccessOracle pao;
    StrategyComposer sc;

    NetDefinition net;
    net.id   = 0;
    net.pins = {{0, 0, 0}, {3, 0, 0}};  // 2 pins on the same layer, 3 apart

    DesignSummary dummy; dummy.nets = {net};
    pao.precompute(dummy, grid);

    auto path = sc.compose_and_route(RandomLogicTag{}, net, grid, pao);
    CHECK_TRUE(!path.empty(), "2-pin random logic produces a non-empty path");
    CHECK_TRUE(path.front() == grid.vertex_at(0, 0, 0), "path starts at pin 0");
    CHECK_TRUE(path.back()  == grid.vertex_at(3, 0, 0), "path ends at pin 1");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 2: Clock network → SpineFishboneRouter → non-empty segment set
// ─────────────────────────────────────────────────────────────────────────────
bool test_clock_network_routing() {
    std::cout << "\n[TEST 2] ClockNetworkTag + 4 sinks → SpineFishboneRouter (H-tree)\n";

    auto grid = make_grid();
    PinAccessOracle pao;
    StrategyComposer sc;

    NetDefinition clk;
    clk.id   = 1;
    clk.name = "clk";
    // Source at centre, 4 sinks spread around it.
    clk.pins = {
        {5, 5, 0},  // source (first pin)
        {1, 1, 0},  // sink 1
        {9, 1, 0},  // sink 2
        {1, 9, 0},  // sink 3
        {9, 9, 0},  // sink 4
    };

    DesignSummary dummy; dummy.nets = {clk};
    pao.precompute(dummy, grid);

    auto segs = sc.compose_and_route(ClockNetworkTag{}, clk, grid, pao);
    CHECK_TRUE(!segs.empty(), "clock H-tree routing produces at least one vertex");
    std::cout << "    H-tree produced " << segs.size() << " vertex segments.\n";
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 3: Memory array context → returns empty (GridFill handled, not A*)
// ─────────────────────────────────────────────────────────────────────────────
bool test_memory_array_returns_empty() {
    std::cout << "\n[TEST 3] MemoryArrayTag → compose_and_route returns empty (GridFill owns this)\n";

    auto grid = make_grid();
    PinAccessOracle pao;
    StrategyComposer sc;

    NetDefinition mem_net;
    mem_net.id   = 2;
    mem_net.pins = {{0, 0, 0}, {0, 5, 0}};

    auto path = sc.compose_and_route(MemoryArrayTag{}, mem_net, grid, pao);
    // Memory nets are stamped by GridFill in GlobalPlanner.  StrategyComposer
    // returns empty to signal "nothing to do here".
    CHECK_TRUE(path.empty(), "MemoryArrayTag context returns empty path (GridFill owns it)");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 4: 3-pin net → TreeRouter (Steiner branch-and-connect)
// ─────────────────────────────────────────────────────────────────────────────
bool test_multi_pin_steiner() {
    std::cout << "\n[TEST 4] RandomLogicTag + 3 pins → TreeRouter (Steiner MST)\n";

    auto grid = make_grid();
    PinAccessOracle pao;
    StrategyComposer sc;

    NetDefinition net3;
    net3.id   = 3;
    net3.pins = {{0, 0, 0}, {5, 0, 0}, {0, 5, 0}};  // Triangle of 3 pins

    DesignSummary dummy; dummy.nets = {net3};
    pao.precompute(dummy, grid);

    auto segments = sc.compose_and_route(RandomLogicTag{}, net3, grid, pao);
    // TreeRouter should connect all 3 pins via 2 branch A* routes.
    CHECK_TRUE(!segments.empty(), "3-pin Steiner routing produces segments");
    std::cout << "    Steiner tree produced " << segments.size() << " vertex segments.\n";
    return true;
}

int main() {
    std::cout << "═══════════════════════════════════════════════════════\n";
    std::cout << " TEST SUITE: StrategyComposer (strategy_composer.hpp)\n";
    std::cout << "═══════════════════════════════════════════════════════\n";

    int passed{0}, failed{0};
    auto run = [&](bool(*fn)(), const char* name) {
        if (fn()) { ++passed; } else { ++failed; std::cout << "  ✗ " << name << " FAILED\n"; }
    };

    run(test_two_pin_random_logic,       "2-pin RandomLogicTag → DetailedGridRouter");
    run(test_clock_network_routing,      "ClockNetworkTag → SpineFishboneRouter H-tree");
    run(test_memory_array_returns_empty, "MemoryArrayTag → empty path");
    run(test_multi_pin_steiner,          "3-pin RandomLogicTag → TreeRouter Steiner");

    std::cout << "\n Results: " << passed << " passed, " << failed << " failed.\n";
    return (failed == 0) ? 0 : 1;
}
