#pragma once
#include <vector>
#include <algorithm>
#include "routing_genetic_astar/core/detailed_grid_router.hpp"
#include "routing_genetic_astar/analysis/pin_access_oracle.hpp"

namespace routing_genetic_astar {

// ═══════════════════════════════════════════════════════════════════════════════
// SpineFishboneRouter — Section 4.6 and Section 5 of architecture_v3.md
//
// Handles CLOCK_NETWORK nets (high fanout) using a balanced H-tree topology.
// Also handles trunk-branch bus structures.
//
// H-Tree algorithm (recursive bisection):
//   1. Compute the bounding box of all sink pins.
//   2. Place a central spine segment at the midpoint of the long axis.
//   3. At each spine endpoint, bisect recursively — each half gets its own
//      sub-spine at the midpoint of its sub-bbox.
//   4. Continue until each leaf spine segment is adjacent to one sink pin.
//   5. Leaf connections are delegated to DetailedGridRouter A*.
//
// Respects EM/IR constraints via EdgeProperties::w_elec (set by ECE before routing).
// ═══════════════════════════════════════════════════════════════════════════════
class SpineFishboneRouter {
public:
    // Build H-tree for a clock net given its source and list of sink pins.
    // Returns the union of all spine + leaf-connection paths.
    [[nodiscard]] std::vector<VertexDesc>
    route_clock_tree(int net_id,
                     VertexDesc trunk_src,
                     const std::vector<GridPoint>& sinks,
                     RoutingGridGraph& grid,
                     const PinAccessOracle& pao,
                     const std::optional<BoundingBox>& bbox = std::nullopt) {
        if (sinks.empty()) return {};

        std::vector<VertexDesc> all_segments;

        // Convert sinks to VertexDesc.
        std::vector<VertexDesc> sink_verts;
        sink_verts.reserve(sinks.size());
        for (const auto& s : sinks)
            sink_verts.push_back(grid.vertex_at(s));

        // Recursively build the H-tree spine segments.
        build_htree(net_id, trunk_src, sink_verts, grid, pao, bbox,
                    all_segments, 0 /*depth*/);

        return all_segments;
    }

private:
    static constexpr int MAX_DEPTH = 4; // Limit recursion depth

    void build_htree(int net_id,
                     VertexDesc src,
                     const std::vector<VertexDesc>& sinks,
                     RoutingGridGraph& grid,
                     const PinAccessOracle& pao,
                     const std::optional<BoundingBox>& bbox,
                     std::vector<VertexDesc>& out,
                     int depth) {
        if (sinks.empty()) return;

        // Base case: connect directly to the single remaining sink via leaf A*.
        if (sinks.size() == 1 || depth >= MAX_DEPTH) {
            DetailedGridRouter leaf;
            for (VertexDesc sink : sinks) {
                auto branch = leaf.route_net(net_id, 0, 1, src, sink, grid, pao, bbox);
                out.insert(out.end(), branch.begin(), branch.end());
            }
            return;
        }

        // Compute bounding box of all sinks.
        int x_min = grid.cols(), x_max = 0;
        int y_min = grid.rows(), y_max = 0;
        for (VertexDesc v : sinks) {
            const auto& p = grid.graph()[v].pos;
            x_min = std::min(x_min, p.x); x_max = std::max(x_max, p.x);
            y_min = std::min(y_min, p.y); y_max = std::max(y_max, p.y);
        }

        // Bisect along the longer axis.
        const bool bisect_x = (x_max - x_min) >= (y_max - y_min);
        const int  mid      = bisect_x ? (x_min + x_max) / 2
                                       : (y_min + y_max) / 2;

        // Split sinks into left/right (or bottom/top) halves.
        std::vector<VertexDesc> left_sinks, right_sinks;
        for (VertexDesc v : sinks) {
            const auto& p = grid.graph()[v].pos;
            if ((bisect_x ? p.x : p.y) <= mid)
                left_sinks.push_back(v);
            else
                right_sinks.push_back(v);
        }

        // Place spine sub-nodes at the midpoints of each half.
        const int src_layer = grid.graph()[src].pos.z;
        const int left_mid_x  = bisect_x ? (x_min + mid) / 2 : (x_min + x_max) / 2;
        const int left_mid_y  = bisect_x ? (y_min + y_max) / 2 : (y_min + mid) / 2;
        const int right_mid_x = bisect_x ? (mid + x_max) / 2 : (x_min + x_max) / 2;
        const int right_mid_y = bisect_x ? (y_min + y_max) / 2 : (mid + y_max) / 2;

        if (!grid.in_bounds(left_mid_x, left_mid_y, src_layer) ||
            !grid.in_bounds(right_mid_x, right_mid_y, src_layer)) {
            // Fallback to direct leaf routing if bisected coordinate is out of bounds.
            DetailedGridRouter leaf;
            for (VertexDesc sink : sinks) {
                auto branch = leaf.route_net(net_id, 0, 1, src, sink, grid, pao, bbox);
                out.insert(out.end(), branch.begin(), branch.end());
            }
            return;
        }

        VertexDesc left_mid  = grid.vertex_at(left_mid_x,  left_mid_y,  src_layer);
        VertexDesc right_mid = grid.vertex_at(right_mid_x, right_mid_y, src_layer);

        // Route trunk from src to each mid-point spine node via leaf A*.
        DetailedGridRouter trunk_router;
        auto lbranch = trunk_router.route_net(net_id, 0, 1, src, left_mid,  grid, pao, bbox);
        auto rbranch = trunk_router.route_net(net_id, 0, 1, src, right_mid, grid, pao, bbox);
        out.insert(out.end(), lbranch.begin(), lbranch.end());
        out.insert(out.end(), rbranch.begin(), rbranch.end());

        // Recurse on each half.
        build_htree(net_id, left_mid,  left_sinks,  grid, pao, bbox, out, depth + 1);
        build_htree(net_id, right_mid, right_sinks, grid, pao, bbox, out, depth + 1);
    }
};

} // namespace routing_genetic_astar
