#pragma once
#include <vector>
#include <algorithm>
#include "routing_genetic_astar/core/detailed_grid_router.hpp"
#include "routing_genetic_astar/analysis/pin_access_oracle.hpp"

namespace routing_genetic_astar {

// ═══════════════════════════════════════════════════════════════════════════════
// TreeRouter — Section 4.6 and Section 5 of architecture_v3.md
//
// Builds a Steiner-like macro tree for multi-pin signal nets, then delegates each
// individual branch to DetailedGridRouter for sub-micron pin weaving.
//
// Algorithm:
//   1. Start with the first pin as the "routed tree" root.
//   2. Repeatedly add the nearest unconnected pin to the routed tree (greedy MST
//      via nearest-neighbor heuristic — O(P²) but P is typically small ≤ 20).
//   3. Each added pin requires one 2-point A* route from any vertex in the
//      already-routed set to the new pin.
//   4. The result is a connected Steiner-like tree covering all pins.
//
// Respects EM/IR-adjusted edge weights from ElectricalConstraintEngine (those
// weights are already stored in EdgeProperties::w_elec — A* sees them automatically).
// ═══════════════════════════════════════════════════════════════════════════════
class TreeRouter {
public:
    // Routes a multi-pin net, returns the union of all branch paths.
    // Pins must be given in NetDefinition order (pin_index == position in vector).
    [[nodiscard]] std::vector<VertexDesc>
    route_steiner(int net_id,
                  const std::vector<GridPoint>& pins,
                  RoutingGridGraph& grid,
                  const PinAccessOracle& pao,
                  const std::optional<BoundingBox>& bbox = std::nullopt) {
        if (pins.size() < 2) return {};

        DetailedGridRouter leaf_router;
        std::vector<VertexDesc> steiner_tree;

        // Translate GridPoints to VertexDesc handles.
        std::vector<VertexDesc> vdesc;
        vdesc.reserve(pins.size());
        for (const auto& p : pins)
            vdesc.push_back(grid.vertex_at(p));

        // Routed set starts with the first pin.
        std::vector<VertexDesc> routed_set{vdesc[0]};
        steiner_tree.push_back(vdesc[0]);

        // Remaining pins to connect.
        std::vector<size_t> pending;
        pending.reserve(vdesc.size() - 1);
        for (size_t i = 1; i < vdesc.size(); ++i) pending.push_back(i);

        while (!pending.empty()) {
            // Find the pending pin nearest (Manhattan) to any vertex in the routed set.
            size_t best_pending_idx = 0;
            size_t best_routed_idx  = 0;
            int    best_dist        = std::numeric_limits<int>::max();

            for (size_t pi = 0; pi < pending.size(); ++pi) {
                for (size_t ri = 0; ri < routed_set.size(); ++ri) {
                    const auto& pp = grid.graph()[vdesc[pending[pi]]].pos;
                    const auto& rp = grid.graph()[routed_set[ri]].pos;
                    const int d = std::abs(pp.x - rp.x) + std::abs(pp.y - rp.y)
                                + std::abs(pp.z - rp.z) * 5;
                    if (d < best_dist) {
                        best_dist        = d;
                        best_pending_idx = pi;
                        best_routed_idx  = ri;
                    }
                }
            }

            // Route from best_routed to best_pending via leaf A*.
            const size_t src_pin = pending[best_pending_idx]; // index in vdesc/pins
            auto branch = leaf_router.route_net(
                net_id,
                static_cast<int>(best_routed_idx),  // src pin index for PinAccessOracle
                static_cast<int>(src_pin),           // dst pin index
                routed_set[best_routed_idx],
                vdesc[src_pin],
                grid, pao, bbox);

            // Append branch to tree (dedup later if needed).
            steiner_tree.insert(steiner_tree.end(), branch.begin(), branch.end());

            // Add ALL newly routed vertices to the routed set for future branch anchoring.
            for (VertexDesc v : branch) routed_set.push_back(v);

            // Remove this pin from pending.
            pending.erase(pending.begin() + static_cast<ptrdiff_t>(best_pending_idx));
        }

        return steiner_tree;
    }
};

} // namespace routing_genetic_astar
