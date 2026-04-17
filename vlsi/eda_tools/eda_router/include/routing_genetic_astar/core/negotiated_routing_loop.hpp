#pragma once
#include <vector>
#include <iostream>
#include "routing_genetic_astar/core/detailed_grid_router.hpp"
#include "routing_genetic_astar/core/history_cost_updater.hpp"
#include "routing_genetic_astar/convergence/convergence_monitor.hpp"
#include "routing_genetic_astar/convergence/ilp_solver.hpp"
#include "routing_genetic_astar/evaluation/adaptive_penalty_controller.hpp"

namespace routing_genetic_astar {

// Forward declaration
class AdaptivePenaltyController;

// ═══════════════════════════════════════════════════════════════════════════════
// NegotiatedRoutingLoop — Section 4.4 and Section 5 of architecture_v3.md
//
// The "Master Traffic Cop" — coordinates all nets, detects conflicts, and drives
// iterative rip-up/reroute cycles until convergence or ILP fallback.
//
// Per-pass state machine (exact match to Section 4.4 flowchart):
//   ROUTE all unresolved nets via DetailedGridRouter A*
//   COLLECT conflicts (try_claim_edge failures → conflicted EdgeDesc list)
//   SEND to ConvergenceMonitor (checks oscillation ring buffer)
//   IF oscillating → isolate subregion → IlpSolver → re-insert locked nets
//   IF converging  → update HistoryCostUpdater (both channels)
//               → rip-up offending nets
//               → AdaptivePenaltyController adjusts W_cong rate
//   LOOP until zero conflicts or max passes
// ═══════════════════════════════════════════════════════════════════════════════

struct RoutableNet {
    int                  id{-1};
    std::vector<GridPoint> pins;     // From NetDefinition
    BoundingBox          corridor{}; // From GlobalPlanner CorridorAssignment
};

class NegotiatedRoutingLoop {
public:
    int max_passes{30};

    void converge(std::vector<RoutableNet>& nets,
                  RoutingGridGraph&         grid,
                  const PinAccessOracle&    pao,
                  HistoryCostUpdater&        hcu,
                  ConvergenceMonitor&        cm,
                  AdaptivePenaltyController& apc) {
        DetailedGridRouter router;
        IlpSolver          ilp_solver;

        std::vector<int> unresolved_ids;
        for (const auto& n : nets) unresolved_ids.push_back(n.id);

        for (int pass = 0; pass < max_passes; ++pass) {
            std::vector<EdgeDesc> pass_conflicts;
            std::vector<int>      newly_conflicted_net_ids;

            // ── ROUTE all unresolved nets ──────────────────────────────────────
            for (const auto& net : nets) {
                const bool is_unresolved =
                    std::find(unresolved_ids.begin(),
                              unresolved_ids.end(), net.id) != unresolved_ids.end();
                if (!is_unresolved) continue;

                if (net.pins.size() < 2) continue;
                VertexDesc src = grid.vertex_at(net.pins[0]);
                VertexDesc dst = grid.vertex_at(net.pins[1]);

                auto path = router.route_net(net.id, 0, 1, src, dst, grid, pao,
                                             std::optional<BoundingBox>{net.corridor});

                if (path.empty()) {
                    // Routing failed → net stays in unresolved list
                    newly_conflicted_net_ids.push_back(net.id);
                }
            }

            // ── COLLECT claim conflicts from graph ─────────────────────────────
            auto [ei, ei_end] = boost::edges(grid.graph());
            for (auto it = ei; it != ei_end; ++it) {
                // An edge is conflicted if its owner is set but the edge also has
                // a non-zero w_cong_history AND a competing route tried to use it.
                // In practice, conflicts arise from try_claim_edge returning false.
                // We detect them by checking w_cong_history > 0 as a proxy here.
                if (grid.graph()[*it].net_owner >= 0 &&
                    grid.graph()[*it].w_cong_history > 0.0f) {
                    pass_conflicts.push_back(*it);
                }
            }

            int conflict_count = static_cast<int>(
                pass_conflicts.size() + newly_conflicted_net_ids.size());

            std::cout << "[NRL pass " << pass << "] conflicts=" << conflict_count << '\n';

            // ── CONVERGENCE MONITOR ────────────────────────────────────────────
            cm.on_iteration(conflict_count, pass, pass_conflicts, grid);

            if (cm.is_converged()) {
                std::cout << "[NRL] Converged at pass " << pass << ".\n";
                return;
            }

            // ── ILP FALLBACK for oscillating subregion ─────────────────────────
            if (cm.is_oscillating()) {
                auto subregion = cm.isolate_oscillating_region();
                if (subregion) {
                    auto sol = ilp_solver.solve(*subregion, grid);
                    if (sol) {
                        // Lock the ILP-assigned routes into the graph.
                        for (auto& [nid, edges] : sol->routes) {
                            for (EdgeDesc e : edges) {
                                (void)grid.try_claim_edge(e, nid);
                                grid.graph()[e].frozen = true; // Locked partial solution
                            }
                            // Remove ILP-resolved nets from unresolved list.
                            auto it2 = std::find(unresolved_ids.begin(),
                                                 unresolved_ids.end(), nid);
                            if (it2 != unresolved_ids.end()) unresolved_ids.erase(it2);
                        }
                    }
                }
            }

            // ── HISTORY COST UPDATE (Congestion channel) ───────────────────────
            hcu.record_all_conflicts(grid, std::span<const EdgeDesc>{pass_conflicts});

            // ── RIP-UP offending nets ──────────────────────────────────────────
            for (int nid : newly_conflicted_net_ids) {
                grid.release_net(nid);
            }

            // ── ADAPTIVE PENALTY CONTROLLER (online feedback) ──────────────────
            apc.adjust(cm, hcu, pass);
        }

        std::cout << "[NRL] Max passes reached without full convergence.\n";
    }
};

} // namespace routing_genetic_astar
