#pragma once
#include <vector>
#include <optional>
#include <deque>
#include "routing_genetic_astar/grid_graph.hpp"

namespace routing_genetic_astar {

// ═══════════════════════════════════════════════════════════════════════════════
// ConvergenceMonitor — Section 5 of architecture_v3.md
//
// Observer for the NegotiatedRoutingLoop.  Called once per pass with the current
// conflict count.  Uses a ring-buffer of the last N counts to detect oscillation:
// when (max − min) of the buffer falls below `oscillation_threshold` for
// `patience_passes` consecutive observations, the loop is oscillating.
//
// On oscillation: identify the bounding box of repeatedly conflicted edges
// and hand it to IlpSolver for a mathematically guaranteed local resolution.
// ═══════════════════════════════════════════════════════════════════════════════

struct SubregionDescriptor {
    BoundingBox bbox;           // Physical region of the chip that is oscillating
    std::vector<int> net_ids;   // Nets involved in the oscillation
};

class ConvergenceMonitor {
public:
    int window_size{8};                 // Ring-buffer depth N (tunable by AdaptivePenaltyController)
    float oscillation_threshold{2.0f};  // Max − min below this → oscillating

    // ── Called by NRL after each pass ─────────────────────────────────────────
    void on_iteration(int conflict_count, int pass_number,
                      const std::vector<EdgeDesc>& conflicted_edges,
                      const RoutingGridGraph& grid) {
        history_.push_back(conflict_count);
        if (static_cast<int>(history_.size()) > window_size)
            history_.pop_front();

        last_conflicted_edges_ = conflicted_edges;
        last_grid_             = &grid;

        // Track the pass number for diagnostics.
        last_pass_ = pass_number;

        // Check for convergence: zero conflicts.
        if (conflict_count == 0) {
            converged_ = true;
            return;
        }

        // Check for oscillation: ring buffer variance is below threshold.
        if (static_cast<int>(history_.size()) == window_size) {
            const float max_c = static_cast<float>(
                *std::max_element(history_.begin(), history_.end()));
            const float min_c = static_cast<float>(
                *std::min_element(history_.begin(), history_.end()));
            oscillating_ = (max_c - min_c) < oscillation_threshold;
        }
    }

    [[nodiscard]] bool is_converged()  const noexcept { return converged_; }
    [[nodiscard]] bool is_oscillating() const noexcept { return oscillating_ && !converged_; }

    // Returns the oscillating subregion if detected; nullopt otherwise.
    // IlpSolver::solve() consumes this to formulate and solve the local IP.
    [[nodiscard]] std::optional<SubregionDescriptor>
    isolate_oscillating_region() const {
        if (!oscillating_ || !last_grid_) return std::nullopt;

        SubregionDescriptor region;
        BoundingBox& bb = region.bbox;
        // Initialise min values at INT_MAX and max values at 0 so that any
        // real edge coordinate will correctly update them on first comparison.
        bb.x_min     = std::numeric_limits<int>::max();
        bb.y_min     = std::numeric_limits<int>::max();
        bb.layer_min = std::numeric_limits<int>::max();
        bb.x_max     = 0;
        bb.y_max     = 0;
        bb.layer_max = 0;

        for (const auto& e : last_conflicted_edges_) {
            const auto& sp = last_grid_->graph()[boost::source(e, last_grid_->graph())].pos;
            const auto& tp = last_grid_->graph()[boost::target(e, last_grid_->graph())].pos;
            // Expand bbox using BOTH endpoints of each conflicted edge.
            for (const auto* pos : {&sp, &tp}) {
                bb.x_min     = std::min(bb.x_min,     pos->x);
                bb.x_max     = std::max(bb.x_max,     pos->x);
                bb.y_min     = std::min(bb.y_min,     pos->y);
                bb.y_max     = std::max(bb.y_max,     pos->y);
                bb.layer_min = std::min(bb.layer_min, pos->z);
                bb.layer_max = std::max(bb.layer_max, pos->z);
            }

            const int owner = last_grid_->graph()[e].net_owner;
            if (owner >= 0) region.net_ids.push_back(owner);
        }
        // Deduplicate net_ids
        std::sort(region.net_ids.begin(), region.net_ids.end());
        region.net_ids.erase(
            std::unique(region.net_ids.begin(), region.net_ids.end()),
            region.net_ids.end());

        return region;
    }

    // AdaptivePenaltyController adjusts the oscillation window size online.
    void set_window_size(int n) noexcept { window_size = std::max(2, n); }

private:
    std::deque<int>         history_;
    std::vector<EdgeDesc>   last_conflicted_edges_;
    const RoutingGridGraph* last_grid_{nullptr};
    int  last_pass_{0};
    bool converged_{false};
    bool oscillating_{false};
};

} // namespace routing_genetic_astar
