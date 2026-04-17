// ═══════════════════════════════════════════════════════════════════════════════
// FILE: test_design_analyzer.cpp  —  Tests for DesignAnalyzer (context classification)
// ═══════════════════════════════════════════════════════════════════════════════
//
// WHAT WE ARE TESTING:
//   That DesignAnalyzer correctly classifies designs into the four RoutingContext
//   types based on fanout distribution and density.
//
// EXPECTED CLASSIFICATIONS:
//   few low-fanout nets, high density       → MemoryArrayTag
//   one net with fanout > 50                → ClockNetworkTag
//   high density + moderate high fanout     → MixedSignalTag
//   normal mixed fanout                     → RandomLogicTag
//
// WHAT EACH CLASSIFICATION MEANS FOR THE ENGINE:
//   MemoryArrayTag  → GlobalPlanner calls GridFill (bypass GA)
//   ClockNetworkTag → StrategyComposer uses SpineFishboneRouter (H-tree)
//   MixedSignalTag  → StrategyComposer uses TreeRouter (Steiner)
//   RandomLogicTag  → Standard PathFinder + DetailedGridRouter
// ─────────────────────────────────────────────────────────────────────────────
#include <cassert>
#include <iostream>
#include <vector>
#include "routing_genetic_astar/analysis/design_analyzer.hpp"
#include "routing_genetic_astar/types.hpp"

using namespace routing_genetic_astar;

#define CHECK_TRUE(condition, msg) \
    do { \
        if (!(condition)) { \
            std::cerr << "FAIL: " << msg << "\n  at " << __FILE__ << ":" << __LINE__ << "\n"; \
            return false; \
        } \
        std::cout << "  PASS: " << msg << "\n"; \
    } while(false)

// Helper: create a net with a given number of pins.
static NetDefinition make_net(int id, int num_pins, const std::string& name = "") {
    NetDefinition net;
    net.id   = id;
    net.name = name.empty() ? "net" + std::to_string(id) : name;
    // Give each pin a unique position so nothing overlaps.
    for (int p = 0; p < num_pins; ++p)
        net.pins.push_back(GridPoint{p, id % 10, 0});
    return net;
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 1: Memory array detection
// High density (many pins per cell), uniform low fanout (2 pins per net)
// ─────────────────────────────────────────────────────────────────────────────
bool test_memory_array_classification() {
    std::cout << "\n[TEST 1] Memory array classification\n";

    // 40 nets × 2 pins each = 80 pins on a 10×10 grid (density=0.80 > 0.3)
    std::vector<NetDefinition> nets;
    for (int i = 0; i < 40; ++i) nets.push_back(make_net(i, 2));

    DesignAnalyzer analyzer;
    auto result = analyzer.analyze(std::span{nets}, 10, 10);

    CHECK_TRUE(result.has_value(), "analyze() returned a value (no error)");
    CHECK_TRUE(std::holds_alternative<MemoryArrayTag>(result->context),
               "dense uniform 2-pin nets classified as MemoryArrayTag");
    CHECK_TRUE(result->avg_fanout == 2.0f, "avg_fanout = 2.0");
    CHECK_TRUE(result->total_nets == 40, "total_nets = 40");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 2: Clock network detection (one very high fanout net)
// ─────────────────────────────────────────────────────────────────────────────
bool test_clock_network_classification() {
    std::cout << "\n[TEST 2] Clock network classification\n";

    std::vector<NetDefinition> nets;
    nets.push_back(make_net(0, 64, "clk")); // 64 sinks → max_fanout > 50
    for (int i = 1; i < 10; ++i) nets.push_back(make_net(i, 3));

    DesignAnalyzer analyzer;
    auto result = analyzer.analyze(std::span{nets}, 100, 100);

    CHECK_TRUE(result.has_value(), "analyze() succeeded");
    CHECK_TRUE(std::holds_alternative<ClockNetworkTag>(result->context),
               "64-sink net classified as ClockNetworkTag");
    CHECK_TRUE(result->max_fanout == 64.0f, "max_fanout = 64.0");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 3: Random logic (normal fanout, normal density)
// ─────────────────────────────────────────────────────────────────────────────
bool test_random_logic_classification() {
    std::cout << "\n[TEST 3] Random logic classification\n";

    std::vector<NetDefinition> nets;
    for (int i = 0; i < 20; ++i)
        nets.push_back(make_net(i, 2 + (i % 3))); // fanout 2, 3, or 4

    DesignAnalyzer analyzer;
    auto result = analyzer.analyze(std::span{nets}, 100, 100);

    CHECK_TRUE(result.has_value(), "analyze() succeeded");
    CHECK_TRUE(std::holds_alternative<RandomLogicTag>(result->context),
               "normal fanout design classified as RandomLogicTag");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 4: Error cases
// ─────────────────────────────────────────────────────────────────────────────
bool test_error_cases() {
    std::cout << "\n[TEST 4] Error cases\n";

    DesignAnalyzer analyzer;
    std::vector<NetDefinition> empty_nets;

    // Empty netlist.
    auto result1 = analyzer.analyze(std::span{empty_nets}, 100, 100);
    CHECK_TRUE(!result1.has_value(), "empty netlist returns error");
    CHECK_TRUE(!result1.error().msg.empty(), "error message is non-empty");

    // Invalid grid dimensions.
    std::vector<NetDefinition> one_net = {make_net(0, 2)};
    auto result2 = analyzer.analyze(std::span{one_net}, 0, 100);
    CHECK_TRUE(!result2.has_value(), "zero rows returns error");

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 5: std::span usage verification
//
// ─── C++ FEATURE: std::span (C++20) ──────────────────────────────────────────
// std::span is a non-owning "view" of a contiguous sequence of elements.
// ANALOGY: A glass window onto someone else's garden — you can see and read
// all the plants through the window without owning or moving them.
//
// Here we verify that analyze() works with BOTH a std::vector and a raw array,
// proving the span interface is properly generic (no copy performed).
// ─────────────────────────────────────────────────────────────────────────────
bool test_span_interface() {
    std::cout << "\n[TEST 5] std::span interface generality\n";

    // Create a C-style array (not a vector) of nets.
    NetDefinition arr[3] = {make_net(0, 2), make_net(1, 3), make_net(2, 2)};

    // std::span can view a raw array without any vector involved.
    std::span<const NetDefinition> view{arr, 3};

    DesignAnalyzer analyzer;
    auto result = analyzer.analyze(view, 50, 50);

    CHECK_TRUE(result.has_value(), "analyze() works with std::span of raw array");
    CHECK_TRUE(result->total_nets == 3, "total_nets = 3 from raw array view");
    return true;
}

int main() {
    std::cout << "═══════════════════════════════════════════════════════\n";
    std::cout << " TEST SUITE: DesignAnalyzer (design_analyzer.hpp)\n";
    std::cout << "═══════════════════════════════════════════════════════\n";

    int passed{0}, failed{0};
    auto run = [&](bool(*fn)(), const char* name) {
        if (fn()) { ++passed; } else { ++failed; std::cout << "  ✗ " << name << " FAILED\n"; }
    };

    run(test_memory_array_classification,  "memory array classification");
    run(test_clock_network_classification, "clock network classification");
    run(test_random_logic_classification,  "random logic classification");
    run(test_error_cases,                  "error cases (empty/invalid input)");
    run(test_span_interface,               "std::span interface generality");

    std::cout << "\n Results: " << passed << " passed, " << failed << " failed.\n";
    return (failed == 0) ? 0 : 1;
}
