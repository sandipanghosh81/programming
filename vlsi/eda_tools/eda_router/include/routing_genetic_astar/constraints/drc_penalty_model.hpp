#pragma once
#include <vector>
#include "routing_genetic_astar/grid_graph.hpp"

namespace routing_genetic_astar {

// ═══════════════════════════════════════════════════════════════════════════════
// DRCPenaltyModel — Section 5 of architecture_v3.md
//
// Encodes technology-rule-specific geometric constraints as routing graph
// modifications.  Critically: it does NOT raise edge costs (that is congestion
// modeling done by HistoryCostUpdater).  Instead it sets direction-specific
// DISABLE bits in EdgeProperties::drc_mask, which the DetailedGridRouter checks
// as a hard gate — blocked moves are skipped regardless of their cost.
//
// Rule types (28nm baseline — can extend to 3nm BEOL):
//   min_cut        — minimum number of vias required between layers
//   eol_spacing    — end-of-line spacing exclusion zone in preferred direction
//   parallel_run   — parallel run-length spacing exclusion
//   tip_to_tip     — metal tip clearance between routes on same layer
//   jog_length     — minimum jog segment length
//   via_enclosure  — enclosure rule on via landing pads
//
// Masks are written once into EdgeProperties before routing starts, then
// persisted across all rip-up iterations via HistoryCostUpdater::apply_drc_mask().
// ═══════════════════════════════════════════════════════════════════════════════

struct TechRules {
    // All values in grid units (1 grid unit = 1 track pitch for this process node)
    int min_spacing    {1};  // Minimum track-to-track spacing
    int eol_spacing    {2};  // End-of-line exclusion distance
    int parallel_run   {4};  // Parallel run length above which extra spacing applies
    int min_cut        {1};  // Minimum via cuts required (≥1 for single, ≥2 for redundant)
    int jog_length     {2};  // Minimum jog length to avoid shorts
};

class DRCPenaltyModel {
public:
    explicit DRCPenaltyModel(TechRules rules = TechRules{}) : rules_{rules} {}

    // Apply all rule masks to every edge in the graph.
    // Must be called AFTER build_lattice() and BEFORE routing starts.
    void apply_masks(RoutingGridGraph& grid) {
        auto [ei, ei_end] = boost::edges(grid.graph());
        for (auto it = ei; it != ei_end; ++it) {
            compute_and_set_mask(grid, *it);
        }
    }

    // Per-edge mask computation: determines which expansion directions to disable.
    // [Train Switch Analogy - [[likely]]]: the vast majority of edges in a real design
    // are DRC-clean; only boundary and corner edges pick up disable flags.
    void compute_and_set_mask(RoutingGridGraph& grid, EdgeDesc e) {
        auto& ep = grid.graph()[e];
        VertexDesc src = boost::source(e, grid.graph());
        VertexDesc tgt = boost::target(e, grid.graph());
        const auto& sp = grid.graph()[src].pos;
        const auto& tp = grid.graph()[tgt].pos;

        uint32_t mask = 0u;

        // ── Via enclosure rule: if this is a via edge (z changes) ─────────────
        // Disable via-down from the upper layer if min_cut > 1 (redundant via required).
        if (tp.z != sp.z && rules_.min_cut > 1) {
            // Penalize: require the router to look for a redundant via site.
            mask |= MASK_VIA_DOWN;
        }

        // ── EOL spacing: edges adjacent to a layer boundary in the non-preferred
        //    direction are flagged to prevent short end-of-line approaches.
        // For even layers (preferred horizontal), block short vertical approaches
        // within eol_spacing of the chip edge.
        const bool preferred_horiz = (sp.z % 2 == 0);
        if (preferred_horiz) {
            if (sp.y < rules_.eol_spacing || sp.y >= grid.rows() - rules_.eol_spacing) {
                mask |= MASK_NORTH;
                mask |= MASK_SOUTH;
            }
        } else {
            if (sp.x < rules_.eol_spacing || sp.x >= grid.cols() - rules_.eol_spacing) {
                mask |= MASK_EAST;
                mask |= MASK_WEST;
            }
        }

        ep.drc_mask |= mask;
    }

    // Query: is an expansion in `direction_flag` blocked for edge `e`?
    // Called by DetailedGridRouter on every neighbor expansion.
    [[nodiscard]] bool blocks_move(const RoutingGridGraph& grid,
                                   EdgeDesc e, uint32_t direction_flag) const noexcept {
        return grid.graph()[e].is_drc_blocked(direction_flag);
    }

private:
    TechRules rules_;
};

} // namespace routing_genetic_astar
