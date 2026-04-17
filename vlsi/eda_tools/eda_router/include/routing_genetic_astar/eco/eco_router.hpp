#pragma once
#include <span>
#include <expected>
#include "routing_genetic_astar/types.hpp"
#include "routing_genetic_astar/grid_graph.hpp"
#include "routing_genetic_astar/analysis/pin_access_oracle.hpp"
#include "routing_genetic_astar/core/history_cost_updater.hpp"
#include "routing_genetic_astar/core/negotiated_routing_loop.hpp"
#include "routing_genetic_astar/convergence/convergence_monitor.hpp"
#include "routing_genetic_astar/evaluation/adaptive_penalty_controller.hpp"

namespace routing_genetic_astar {

// ═══════════════════════════════════════════════════════════════════════════════
// EcoRouter — Section 5 of architecture_v3.md
//
// Entry point for Engineering Change Order (ECO) flows.
// Re-routes a small delta of changed nets while holding all others frozen.
//
// Protocol:
//   1. Call grid.freeze_net(id) for every unchanged net → those edges become
//      permanent obstructions (contribute capacity, ineligible for rip-up).
//   2. Call converge() on only the delta nets via NegotiatedRoutingLoop.
//
// [The Glass Window Analogy]: std::span<const int> is the window frame placed over
// just the changed net IDs in the caller's array — no copy of the full net list,
// zero allocation overhead, direct pointer-width view into the existing storage.
// ═══════════════════════════════════════════════════════════════════════════════
class EcoRouter {
public:
    struct EcoResult { bool success{false}; int rerouted_count{0}; };

    [[nodiscard]] std::expected<EcoResult, RoutingError>
    reroute(RoutingGridGraph&         grid,
            std::span<const int>      changed_net_ids,
            const std::vector<RoutableNet>& all_nets,
            const PinAccessOracle&    pao,
            HistoryCostUpdater&        hcu,
            ConvergenceMonitor&        cm,
            AdaptivePenaltyController& apc) {
        if (changed_net_ids.empty())
            return std::unexpected(RoutingError{"EcoRouter: no changed nets provided"});

        // Step 1: Freeze ALL unchanged nets.
        for (const auto& net : all_nets) {
            bool is_changed = false;
            for (int cid : changed_net_ids) {
                if (net.id == cid) { is_changed = true; break; }
            }
            if (!is_changed) grid.freeze_net(net.id);
        }

        // Step 2: Collect only the delta RoutableNet objects.
        std::vector<RoutableNet> delta_nets;
        delta_nets.reserve(changed_net_ids.size());
        for (int cid : changed_net_ids) {
            for (const auto& rn : all_nets) {
                if (rn.id == cid) { delta_nets.push_back(rn); break; }
            }
        }

        // Rip-up the changed nets so they can be re-routed fresh.
        for (int cid : changed_net_ids) grid.release_net(cid);

        // Step 3: Re-route via NegotiatedRoutingLoop on the delta nets only.
        NegotiatedRoutingLoop nrl;
        nrl.converge(delta_nets, grid, pao, hcu, cm, apc);

        return EcoResult{true, static_cast<int>(delta_nets.size())};
    }
};

} // namespace routing_genetic_astar
