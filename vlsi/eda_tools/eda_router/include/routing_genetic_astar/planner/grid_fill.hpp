#pragma once
// grid_fill.hpp needs std::expected (C++23) and std::string for PlannerError.
#include <expected>
#include <string>
#include <vector>
#include "routing_genetic_astar/grid_graph.hpp"
#include "routing_genetic_astar/types.hpp"

namespace routing_genetic_astar {

// Forward declarations (prevent circular includes with global_planner.hpp)
struct CorridorAssignment;
struct PlannerError;

// ═══════════════════════════════════════════════════════════════════════════════
// GridFill — Section 4.2 and Section 4.6 of architecture_v3.md
//
// Deterministic track assignment for MEMORY_ARRAY context.
// Memory bitlines and wordlines form a solved combinatorial structure —
// heuristic GA search is wasteful here.  GridFill assigns them analytically:
//   Even nets (bitlines)  → vertical tracks on even-index metal layers
//   Odd nets  (wordlines) → horizontal tracks on odd-index metal layers
//
// Returns a CorridorAssignment with all corridors pre-filled.
// GlobalPlanner::plan() calls this directly (skips all GA evolution).
// ═══════════════════════════════════════════════════════════════════════════════
class GridFill {
public:
    // [[nodiscard]]: If the caller discards the filled assignment, every net in
    // the memory array ends up with NO corridor → routing will fail silently.
    [[nodiscard]] std::expected<CorridorAssignment, PlannerError>
    stamp_array(const DesignSummary& design, const RoutingGridGraph& grid);
};

} // namespace routing_genetic_astar
