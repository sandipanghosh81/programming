#pragma once
#include <mutex>
#include <span>
#include <ranges>
#include "routing_genetic_astar/grid_graph.hpp"

namespace routing_genetic_astar {

// ═══════════════════════════════════════════════════════════════════════════════
// HistoryCostUpdater — Section 4.5 and Section 5 of architecture_v3.md
//
// The "Toll Booth Operator" maintains per-edge historical penalty state across
// all NegotiatedRoutingLoop passes.  Two INDEPENDENT signal channels feed it:
//
//   Channel 1 — Congestion: capacity-overflow events from NegotiatedRoutingLoop.
//               Stored as w_cong_history on EdgeProperties.  Scaled by W_cong.
//   Channel 2 — DRC masks:  geometric rule violations from DRCPenaltyModel.
//               Stored as drc_mask bits on EdgeProperties.  Not a cost — a gate.
//
// Conflating these channels was a correctness bug in v2.  They are weighted
// independently here so DRC avoidance cannot be diluted by congestion dynamics.
//
// Thread-safety: all mutating calls are serialized with a mutex; it is called
// only by the NRL orchestrator, not from parallel A* threads.
// ═══════════════════════════════════════════════════════════════════════════════
class HistoryCostUpdater {
public:
    // Tunable multipliers.  AdaptivePenaltyController adjusts w_cong_ online.
    float w_cong{1.5f};  // Congestion channel weight multiplier
    float w_drc{1.0f};   // DRC channel scale (mask is boolean, but penalty can ramp)

    // ── Congestion Channel ────────────────────────────────────────────────────
    // Called by NRL after each pass for every edge that experienced capacity overflow.
    // Increments w_cong_history on the EdgeProperties by w_cong × 1 overflow event.
    void record_congestion_conflict(RoutingGridGraph& grid, EdgeDesc e) {
        std::lock_guard lock(mtx_);
        grid.graph()[e].w_cong_history += w_cong;
    }

    // Bulk update: record conflicts for an entire vector of conflicted edges
    void record_all_conflicts(RoutingGridGraph& grid, std::span<const EdgeDesc> conflicts) {
        std::lock_guard lock(mtx_);
        for (const auto& e : conflicts)
            grid.graph()[e].w_cong_history += w_cong;
    }

    // ── DRC Channel ──────────────────────────────────────────────────────────
    // Called by DRCPenaltyModel to set geometric avoidance masks.
    // Ors direction-disable bits into the drc_mask of the edge; never clears them.
    // These persist across rip-up iterations (Section 4.5: "distinct channel, persists").
    void apply_drc_mask(RoutingGridGraph& grid, EdgeDesc e, uint32_t direction_flags) {
        std::lock_guard lock(mtx_);
        grid.graph()[e].drc_mask |= direction_flags;
    }

    // ── AdaptivePenaltyController interface ───────────────────────────────────
    // The P-controller calls this to ramp or back off the congestion penalty rate
    // online during a routing run without touching the DRC masks.
    void set_w_cong(float new_w_cong) {
        std::lock_guard lock(mtx_);
        w_cong = new_w_cong;
    }

    [[nodiscard]] float get_w_cong() const {
        std::lock_guard lock(mtx_);
        return w_cong;
    }

    // ── Query: effective toll for edge (read by DetailedGridRouter) ───────────
    // [Unopened Mail Analogy]: caller MUST use the returned weight — discarding it
    // would mean routing with stale cost, producing incorrect convergence.
    [[nodiscard]] float edge_toll(const RoutingGridGraph& grid, EdgeDesc e) const noexcept {
        return grid.graph()[e].effective_weight();
    }

private:
    mutable std::mutex mtx_;
};

} // namespace routing_genetic_astar
