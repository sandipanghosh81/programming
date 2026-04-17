// ═══════════════════════════════════════════════════════════════════════════════
// FILE: routing_genetic_astar/include/routing_genetic_astar/analysis/context_classifier.hpp
// PURPOSE: Physical density math to classify design context for StrategyComposer
// ═══════════════════════════════════════════════════════════════════════════════
//
// WHAT THIS IS:
//   A lightweight classifier that reads the DesignSummary produced by
//   DesignAnalyzer and returns the correct RoutingContext tag.
//   StrategyComposer uses this tag to pick the right routing algorithm.
//
// FOUR CONTEXTS (matching architecture_v3.md Section 2.3):
//
//   MemoryArrayTag:
//     Dense uniform-fanout nets.  Bitlines and wordlines repeat in a pattern.
//     → GlobalPlanner bypasses GA and calls GridFill directly.
//     DETECTION: pin_density > 0.3 AND max_fanout < 4
//
//   ClockNetworkTag:
//     One or a few nets with very high fanout (> 50 sinks).
//     → StrategyComposer uses SpineFishboneRouter (H-tree).
//     DETECTION: max_fanout > 50
//
//   MixedSignalTag:
//     Moderate density AND moderate high fanout (analog + digital combined).
//     → StrategyComposer uses TreeRouter (Steiner MST).
//     DETECTION: pin_density > 0.15 AND avg_fanout > 5
//
//   RandomLogicTag:
//     Standard digital logic: varied fanout, moderate density.
//     → Standard PathFinder + NRL.
//     DETECTION: everything else
//
// WHY SEPARATE FROM DesignAnalyzer?
//   DesignAnalyzer builds the DesignSummary (pure data collection).
//   ContextClassifier READS the summary to CLASSIFY it.
//   Separation allows the classifier thresholds to be tuned without recompiling
//   DesignAnalyzer, and allows unit-testing each independently.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once
#include "routing_genetic_astar/types.hpp"
#include "routing_genetic_astar/analysis/design_analyzer.hpp"

namespace routing_genetic_astar {

class ContextClassifier {
public:
    // ── TUNABLE THRESHOLDS ────────────────────────────────────────────────────
    // These values are calibrated for 28nm standard cell designs.
    // Adjust for different process nodes or flow types.
    // AdaptivePenaltyController can update these at runtime (future feature).
    float clock_fanout_threshold    = 50.0f;  // max_fanout > 50 → ClockNetworkTag
    float memory_density_threshold  = 0.3f;   // pin_density > 0.3 AND max_fanout < 4
    float memory_fanout_max         = 4.0f;   // above this, not a memory array
    float mixed_density_threshold   = 0.15f;  // pin_density > 0.15
    float mixed_fanout_threshold    = 5.0f;   // avg_fanout > 5 → MixedSignal if also dense

    // ── classify() ────────────────────────────────────────────────────────────
    // WHAT IT DOES:
    //   Reads numeric metrics from the DesignSummary and returns the correct
    //   RoutingContext std::variant (one of four tag types).
    //
    // DECISION TREE (evaluated top-to-bottom, first match wins):
    //   1. max_fanout > clock_fanout_threshold   → ClockNetworkTag   (H-tree)
    //   2. density > 0.3 AND max_fanout < 4      → MemoryArrayTag    (GridFill)
    //   3. density > 0.15 AND avg_fanout > 5     → MixedSignalTag    (TreeRouter)
    //   4. Everything else                       → RandomLogicTag    (PathFinder+NRL)
    //
    // ANALOGY: A triage nurse assessing incoming patients.
    //   Critical (max_fanout≫) → ICU (clock router)
    //   Memory ward pattern    → Memory ward (GridFill)
    //   Mixed symptoms         → General ward (Steiner)
    //   Standard case          → Outpatient (PathFinder)
    [[nodiscard]] RoutingContext classify(const DesignSummary& summary) const noexcept {
        // Rule 1: High fanout → clock network
        if (summary.max_fanout > clock_fanout_threshold)
            return ClockNetworkTag{};

        // Rule 2: Dense + low fanout → memory array (bitlines/wordlines)
        if (summary.pin_density > memory_density_threshold &&
            summary.max_fanout < memory_fanout_max)
            return MemoryArrayTag{};

        // Rule 3: Moderate density + elevated avg fanout → mixed signal
        if (summary.pin_density > mixed_density_threshold &&
            summary.avg_fanout > mixed_fanout_threshold)
            return MixedSignalTag{};

        // Rule 4: Default — standard random logic
        return RandomLogicTag{};
    }
};

} // namespace routing_genetic_astar
