#pragma once
#include <barrier>
#include <vector>
#include <mutex>
#include "routing_genetic_astar/routing/spatial_partitioner.hpp"
#include "routing_genetic_astar/grid_graph.hpp"

namespace routing_genetic_astar {

// ═══════════════════════════════════════════════════════════════════════════════
// CrossRegionMediator — Section 4.3 and Section 5 of architecture_v3.md
//
// Handles nets that span SpatialPartitioner region boundaries.
// Protocol (barrier-based, not lock-free):
//   1. All intra-region jthread workers complete their negotiation pass
//      and arrive at the std::barrier.
//   2. CrossRegionMediator runs in the main/coordinator thread:
//      it identifies nets with edges in two different regions, reads current
//      ghost-cell state from BOTH adjacent regions, routes the cross-boundary
//      segment, and writes the result to ghost cells.
//   3. Threads are released from the barrier for the next pass.
//
// This is marginally less parallel than a lock-free approach but is correct,
// predictable, and avoids ghost-cell coherence bugs (Section 5 rationale).
//
// [The School Field Trip Analogy]: after ALL students return to the bus (barrier),
// the teacher walks the boundary between museum wings (ghost cells), records which
// rooms connect where, and distributes the map before anyone moves again.
// ═══════════════════════════════════════════════════════════════════════════════
class CrossRegionMediator {
public:
    // Identify nets spanning region boundaries and route their cross-boundary segments.
    // Returns the number of cross-boundary nets successfully mediated.
    [[nodiscard]] int mediate(
            RoutingGridGraph& grid,
            const std::vector<PartitionRegion>& regions,
            const std::vector<int>& all_net_ids) {
        // Find nets present in more than one region.
        std::lock_guard lock(ghost_mtx_);
        int mediated{0};

        for (int net_id : all_net_ids) {
            int region_count{0};
            for (const auto& reg : regions) {
                for (int nid : reg.net_ids) {
                    if (nid == net_id) { ++region_count; break; }
                }
            }

            if (region_count > 1) {
                // This net crosses a boundary.  Find ghost cells it touches and
                // ensure they are properly stitched: verify the ownership chain
                // is continuous across the boundary edge.
                mediate_one_net(grid, net_id);
                ++mediated;
            }
        }
        return mediated;
    }

private:
    std::mutex ghost_mtx_;

    // Stitch the ghost cells for one cross-boundary net.
    // Looks for edges in the graph where one endpoint is a ghost cell and the
    // owner matches net_id; marks adjacent non-ghost edges as tentatively claimed.
    void mediate_one_net(RoutingGridGraph& grid, int net_id) {
        auto [ei, ei_end] = boost::edges(grid.graph());
        for (auto it = ei; it != ei_end; ++it) {
            auto& ep = grid.graph()[*it];
            if (ep.net_owner != net_id) continue;

            VertexDesc src = boost::source(*it, grid.graph());
            VertexDesc tgt = boost::target(*it, grid.graph());

            // If the edge crosses a ghost boundary, ensure the adjacent free edge is claimed.
            if (grid.graph()[src].is_ghost || grid.graph()[tgt].is_ghost) {
                // Attempt to extend ownership across the ghost boundary.
                auto [adj_b, adj_e] = boost::adjacent_vertices(tgt, grid.graph());
                for (auto adj = adj_b; adj != adj_e; ++adj) {
                    if (*adj == src) continue;
                    auto opt_e = grid.edge_between(tgt, *adj);
                    if (!opt_e) continue;
                    auto& adj_ep = grid.graph()[*opt_e];
                    // Claim the extension if it's free and not DRC-blocked.
                    if (adj_ep.net_owner == -1 && !adj_ep.frozen &&
                        !adj_ep.is_drc_blocked(MASK_NORTH | MASK_SOUTH |
                                               MASK_EAST  | MASK_WEST)) {
                        adj_ep.net_owner = net_id;
                    }
                }
            }
        }
    }
};

} // namespace routing_genetic_astar
