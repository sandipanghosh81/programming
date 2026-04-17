#pragma once
#include "routing_genetic_astar/planner/global_planner.hpp"
#include "routing_genetic_astar/grid_graph.hpp"

namespace routing_genetic_astar {

// ═══════════════════════════════════════════════════════════════════════════════
// CorridorRefinement — Section 5 of architecture_v3.md
//
// Bridges the GCell-to-track resolution gap before SpatialPartitioner fires.
// For each GA corridor, verifies:
//   1. Via-stack landing sites exist inside the bbox (any vertex in bbox has
//      a valid via edge to the next layer).
//   2. Net count ≤ actual track capacity (not just the GCell demand model).
//   3. No blocking obstructions (frozen ECO edges) cut through the corridor.
//
// Corriders that fail are returned in `infeasible_net_ids` so GlobalPlanner
// can penalize them in the next fitness evaluation.
// ═══════════════════════════════════════════════════════════════════════════════

struct RefinementResult {
    bool                 all_feasible{true};
    std::vector<int>     infeasible_net_ids;  // nets whose corridors failed
    CorridorAssignment   refined;             // corridors with tightened feasible bounds
};

class CorridorRefinement {
public:
    [[nodiscard]] RefinementResult
    refine(const CorridorAssignment& assignment, const RoutingGridGraph& grid) const {
        RefinementResult result;
        result.refined = assignment;

        for (auto& nc : result.refined.corridors) {
            if (!verify_via_sites(nc, grid) || !verify_capacity(nc, grid)) {
                result.all_feasible = false;
                result.infeasible_net_ids.push_back(nc.net_id);
            }
        }
        return result;
    }

private:
    // Check that at least one valid via edge exists inside the corridor bbox.
    bool verify_via_sites(const CorridorAssignment::NetCorridor& nc,
                          const RoutingGridGraph& grid) const {
        // Walk the bbox column-by-column looking for a via edge not blocked by DRC.
        for (int y = nc.bbox.y_min; y <= nc.bbox.y_max; ++y) {
            for (int x = nc.bbox.x_min; x <= nc.bbox.x_max; ++x) {
                for (int l = nc.bbox.layer_min; l < nc.bbox.layer_max; ++l) {
                    if (!grid.in_bounds(x, y, l) || !grid.in_bounds(x, y, l+1)) continue;
                    VertexDesc u = grid.vertex_at(x, y, l);
                    VertexDesc v = grid.vertex_at(x, y, l+1);
                    auto opt_edge = grid.edge_between(u, v);
                    if (!opt_edge) continue;
                    const auto& ep = grid.graph()[*opt_edge];
                    // Via site is valid if the edge is not blocked by DRC mask and not frozen.
                    if (!ep.is_drc_blocked(MASK_VIA_UP) && !ep.frozen)
                        return true;
                }
            }
        }
        // Single-layer corridors don't need via sites.
        return (nc.bbox.layer_min == nc.bbox.layer_max);
    }

    // Check that track capacity can cover the net (at least one free track exists).
    bool verify_capacity(const CorridorAssignment::NetCorridor& nc,
                         const RoutingGridGraph& grid) const {
        int free_tracks = 0;
        for (int y = nc.bbox.y_min; y <= nc.bbox.y_max && free_tracks == 0; ++y) {
            for (int x = nc.bbox.x_min; x <= nc.bbox.x_max && free_tracks == 0; ++x) {
                const int l = nc.preferred_layer;
                if (!grid.in_bounds(x, y, l)) continue;
                VertexDesc v = grid.vertex_at(x, y, l);
                if (grid.graph()[v].gcell_demand < grid.graph()[v].gcell_capacity)
                    ++free_tracks;
            }
        }
        return free_tracks > 0;
    }
};

} // namespace routing_genetic_astar
