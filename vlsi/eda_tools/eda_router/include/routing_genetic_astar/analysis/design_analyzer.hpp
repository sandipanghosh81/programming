#pragma once
#include <span>
#include <algorithm>
#include <expected>
#include <numeric>
#include "routing_genetic_astar/types.hpp"
#include "routing_genetic_astar/grid_graph.hpp"

namespace routing_genetic_astar {

struct AnalysisError { std::string msg; };

// ═══════════════════════════════════════════════════════════════════════════════
// DesignAnalyzer — Section 4.1 and Section 5 of architecture_v3.md
//
// Inspects netlist statistics to:
//   1. Compute density, fanout distribution, and net length histogram.
//   2. Classify the routing context: RANDOM_LOGIC / MEMORY_ARRAY / CLOCK_NETWORK
//      / MIXED_SIGNAL.  This classification drives every downstream strategy
//      decision in StrategyComposer and GlobalPlanner.
// ═══════════════════════════════════════════════════════════════════════════════
class DesignAnalyzer {
public:
    // [The Package Delivery Analogy]: std::expected forces callers to explicitly
    // handle the AnalysisError rather than silently dropping a null result.
    [[nodiscard]] std::expected<DesignSummary, AnalysisError>
    analyze(std::span<const NetDefinition> nets, int grid_rows, int grid_cols) {
        if (nets.empty())
            return std::unexpected(AnalysisError{"No nets provided to DesignAnalyzer"});
        if (grid_rows <= 0 || grid_cols <= 0)
            return std::unexpected(AnalysisError{"Invalid grid dimensions"});

        DesignSummary s;
        s.nets       = {nets.begin(), nets.end()};
        s.total_nets = static_cast<int>(nets.size());

        float total_fanout{0.0f};
        float max_fanout{0.0f};
        int   total_pins{0};

        // Uniformity check: memory arrays have highly regular fanout across all nets.
        const int first_fanout = static_cast<int>(nets[0].pins.size());
        bool      uniform_fanout = true;

        for (const auto& net : nets) {
            const int fanout = static_cast<int>(net.pins.size());
            total_fanout += static_cast<float>(fanout);
            max_fanout    = std::max(max_fanout, static_cast<float>(fanout));
            total_pins   += fanout;
            if (fanout != first_fanout) uniform_fanout = false;
        }

        s.total_pins = total_pins;
        s.avg_fanout = total_fanout / static_cast<float>(s.total_nets);
        s.max_fanout = max_fanout;
        s.density    = static_cast<float>(total_pins)
                       / static_cast<float>(grid_rows * grid_cols);

        // ── Context classification (Section 4.1 of architecture_v3.md) ────────
        // Priority order: MEMORY_ARRAY > CLOCK_NETWORK > MIXED_SIGNAL > RANDOM_LOGIC
        if (uniform_fanout && s.avg_fanout <= 4.0f && s.density > 0.3f) {
            // Highly regular, dense, low-fanout → bitline/wordline memory structure.
            // Deterministic GridFill is used; GA heuristic search is wasteful here.
            s.context = MemoryArrayTag{};
        } else if (s.max_fanout > 50.0f) {
            // Extreme fanout → clock distribution network requiring H-tree topology.
            s.context = ClockNetworkTag{};
        } else if (s.density > 0.7f && s.max_fanout > 20.0f) {
            // Dense with moderate high-fanout → mixed-signal region.
            s.context = MixedSignalTag{};
        } else {
            // Standard random logic: PathFinder negotiation is the right tool.
            s.context = RandomLogicTag{};
        }

        return s;
    }
};

} // namespace routing_genetic_astar
