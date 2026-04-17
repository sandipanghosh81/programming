#pragma once
#include <boost/container/flat_map.hpp>
#include "routing_genetic_astar/grid_graph.hpp"

namespace routing_genetic_astar {

// ═══════════════════════════════════════════════════════════════════════════════
// CongestionOracle — Section 5 of architecture_v3.md
//
// Provides O(1) track-capacity lookups during active routing.
// Uses boost::container::flat_map keyed by a compact GCell index.
//
// [The Rolodex Analogy]: flat_map stores GCell demand entries in a sorted flat
// vector rather than a red-black tree.  The CPU can binary-search through cache-
// local memory like flipping rolodex cards — far faster than chasing pointers
// through a scattered std::map node forest.
// ═══════════════════════════════════════════════════════════════════════════════
class CongestionOracle {
public:
    explicit CongestionOracle(const RoutingGridGraph& grid) : grid_(&grid) {}

    // Rebuild the flat demand snapshot from the current graph state.
    // Called once per NegotiatedRoutingLoop pass before any A* lookups.
    void rebuild(const RoutingGridGraph& g) {
        demand_cache_.clear();
        auto [vi, vi_end] = boost::vertices(g.graph());
        for (auto it = vi; it != vi_end; ++it) {
            const auto& vp  = g.graph()[*it];
            // Encode 3D coordinate into a single int key: z*R*C + y*C + x.
            const int key = vp.pos.z * g.rows() * g.cols()
                          + vp.pos.y * g.cols()
                          + vp.pos.x;
            demand_cache_[key] = vp.gcell_demand / std::max(1.0f, vp.gcell_capacity);
        }
    }

    // O(1) utilization ratio for a vertex (demand / capacity, clamped to [0, ∞)).
    // Used by GlobalPlanner fitness and HistoryCostUpdater W_cong scaling.
    [[nodiscard]] float utilization(VertexDesc v) const noexcept {
        const auto& vp = grid_->graph()[v];
        const int key  = vp.pos.z * grid_->rows() * grid_->cols()
                       + vp.pos.y * grid_->cols()
                       + vp.pos.x;
        // [The Glass Window Analogy]: flat_map::find follows a pointer window into
        // contiguous memory — no allocation, no indirection, no overhead.
        auto it = demand_cache_.find(key);
        if (it != demand_cache_.end()) return it->second;
        return 0.0f;
    }

    // Returns true if the GCell is over-capacity (demand > capacity).
    [[nodiscard]] bool is_overloaded(VertexDesc v) const noexcept {
        return utilization(v) > 1.0f;
    }

private:
    const RoutingGridGraph*                      grid_;
    boost::container::flat_map<int, float>       demand_cache_;
};

} // namespace routing_genetic_astar
