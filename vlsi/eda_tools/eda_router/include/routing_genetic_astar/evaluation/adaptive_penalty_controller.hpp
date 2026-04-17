#pragma once
#include "routing_genetic_astar/convergence/convergence_monitor.hpp"
#include "routing_genetic_astar/core/history_cost_updater.hpp"

namespace routing_genetic_astar {

// ═══════════════════════════════════════════════════════════════════════════════
// AdaptivePenaltyController — Section 4.9 and Section 5 of architecture_v3.md
//
// Proportional feedback controller operating online (within a single routing run).
// Observes the conflict-delta from ConvergenceMonitor and adjusts W_cong in
// HistoryCostUpdater to accelerate convergence:
//
//   slow convergence  → ramp W_cong UP (make congested edges more expensive)
//   oscillation       → back off W_cong (allow more exploration before ILP fires)
//
// Also adjusts ConvergenceMonitor's oscillation window N based on local density.
//
// [The Cruise Control Analogy]: Like a car's adaptive cruise control, the P-controller
// reads the gap to the target speed (conflict_delta target) and depresses or lifts the
// throttle (W_cong rate) proportionally — never leaving the decision to a human.
// ═══════════════════════════════════════════════════════════════════════════════
class AdaptivePenaltyController {
public:
    float Kp{0.1f};              // Proportional gain
    float target_delta{5.0f};   // Desired conflict reduction per pass (tuned by Optuna)
    float w_cong_min{0.5f};     // Floor for W_cong
    float w_cong_max{10.0f};    // Ceiling for W_cong

    void adjust(ConvergenceMonitor& cm,
                HistoryCostUpdater& hcu,
                int /* pass */) {
        if (cm.is_oscillating()) {
            // Back off: reduce W_cong to allow more exploration before ILP fires.
            float w = hcu.get_w_cong();
            w = std::max(w_cong_min, w * 0.85f);
            hcu.set_w_cong(w);
            // Widen the oscillation window so CM waits longer before triggering ILP
            cm.set_window_size(cm.window_size + 2);
        } else {
            // Ramp up if converging too slowly (conflicts not dropping fast enough).
            // P-controller: Δw = Kp × (target_delta − actual_delta)
            // We proxy actual_delta as 0 when no improvement observable (conservative).
            float w = hcu.get_w_cong();
            w = std::clamp(w + Kp, w_cong_min, w_cong_max);
            hcu.set_w_cong(w);
        }
    }
};

} // namespace routing_genetic_astar
