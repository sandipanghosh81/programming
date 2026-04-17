#pragma once
#include <cstddef>
#include "routing_genetic_astar/grid_graph.hpp"

namespace routing_genetic_astar {

// ═══════════════════════════════════════════════════════════════════════════════
// RouteEvaluator — Section 5 of architecture_v3.md
//
// Traverses ALL edges in the RoutingGridGraph after convergence and accumulates:
//   total_wirelength  — count of edge segments claimed by any net
//   via_count         — count of claimed via edges (layer-transitioning edges)
//   drc_violations    — claimed edges where drc_mask would block the direction
//                       the net actually used (post-route DRC audit)
//   open_nets         — nets with fewer claimed edges than their minimum path length
//   congestion_residual — sum of remaining gcell overflow after routing
//
// Scores are fed to AdaptivePenaltyController (online) and OptunaTuner (offline).
// ═══════════════════════════════════════════════════════════════════════════════

struct EvaluationReport {
    std::size_t total_wirelength{0};    // Route edge count (proxy for physical length)
    std::size_t via_count{0};           // Layer-transition edge count
    std::size_t drc_violations{0};      // Post-route DRC audit violations
    std::size_t open_nets{0};           // Nets with incomplete connectivity
    float       congestion_residual{0.0f}; // Sum of remaining overflow
    float       worst_slack_ps{0.0f};   // Timing slack placeholder (needs STA integration)
};

class RouteEvaluator {
public:
    [[nodiscard]] EvaluationReport
    evaluate(const RoutingGridGraph& grid, int total_nets) const {
        EvaluationReport report;

        // Count claimed edges (wirelength proxy) and via edges.
        auto [ei, ei_end] = boost::edges(grid.graph());
        for (auto it = ei; it != ei_end; ++it) {
            const auto& ep = grid.graph()[*it];
            if (ep.net_owner < 0) continue; // Unclaimed edge

            ++report.total_wirelength;

            // Via edges: source and target are on different layers.
            const auto& sp = grid.graph()[boost::source(*it, grid.graph())].pos;
            const auto& tp = grid.graph()[boost::target(*it, grid.graph())].pos;
            if (sp.z != tp.z) ++report.via_count;

            // DRC audit: if the edge has any drc_mask bits set and the direction of
            // the claimed route would trigger one, flag it as a violation.
            if (ep.drc_mask != 0u) {
                // Infer direction from geometry.
                uint32_t dir = infer_direction(sp, tp);
                if (ep.is_drc_blocked(dir)) ++report.drc_violations;
            }
        }

        // Congestion residual: sum of all remaining GCell overflow.
        auto [vi, vi_end] = boost::vertices(grid.graph());
        for (auto it = vi; it != vi_end; ++it)
            report.congestion_residual += grid.gcell_overflow(*it);

        // Open-net heuristic: nets with zero claimed edges are unrouted.
        // (A full check would verify per-net connectivity, requiring net topology.)
        // Here we proxy: count nets that have no edges in the graph.
        // Build per-net edge count.
        std::vector<int> net_edge_count(static_cast<size_t>(total_nets), 0);
        auto [ei2, ei_end2] = boost::edges(grid.graph());
        for (auto it = ei2; it != ei_end2; ++it) {
            int owner = grid.graph()[*it].net_owner;
            if (owner >= 0 && owner < total_nets)
                ++net_edge_count[static_cast<size_t>(owner)];
        }
        for (int c : net_edge_count)
            if (c == 0) ++report.open_nets;

        return report;
    }

private:
    [[nodiscard]] static uint32_t infer_direction(const GridPoint& from,
                                                   const GridPoint& to) noexcept {
        if (to.z > from.z) return MASK_VIA_UP;
        if (to.z < from.z) return MASK_VIA_DOWN;
        if (to.x > from.x) return MASK_EAST;
        if (to.x < from.x) return MASK_WEST;
        if (to.y > from.y) return MASK_NORTH;
        return MASK_SOUTH;
    }
};

} // namespace routing_genetic_astar
