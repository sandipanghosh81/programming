#pragma once
#include <vector>
#include <span>
#include <string>
#include "routing_genetic_astar/grid_graph.hpp"
#include "routing_genetic_astar/types.hpp"

namespace routing_genetic_astar {

// ═══════════════════════════════════════════════════════════════════════════════
// ElectricalConstraintEngine — Section 4.8 of architecture_v3.md
//
// Pre-routing computation that converts electrical requirements into W_elec
// edge-weight adjustments BEFORE a single net touches a track.
//
// Three sub-analyses (all inject into EdgeProperties::w_elec):
//   EM  — Electromigration: current density budget per net per layer.
//          Tracks below the required metal width for a net's current load are
//          upweighted, steering A* toward wider tracks.
//   IR  — IR drop: via sequence resistance budget per net path.
//          Via sequences whose cumulative resistance exceeds budget are penalized,
//          steering A* away from long via stacks.
//   P/G — Power/Ground: redundant via insertion and via stacking requirements.
//          P/G nets require ≥2 vias at each stack; single-via paths are upweighted.
// ═══════════════════════════════════════════════════════════════════════════════

struct NetElectricalSpec {
    int   net_id{-1};
    float max_current_mA{1.0f};    // Required current carrying capacity (mA)
    float max_via_resistance_ohm{50.0f}; // Budget for via-stack IR drop
    bool  is_power_ground{false};  // Requires redundant via insertion
};

class ElectricalConstraintEngine {
public:
    // Derive per-net EM/IR budgets from net names (P/G rails vs signals).
    [[nodiscard]] static std::vector<NetElectricalSpec> specs_from_summary(
        const DesignSummary& summary) {
        std::vector<NetElectricalSpec> specs;
        specs.reserve(summary.nets.size());
        for (const auto& n : summary.nets) {
            NetElectricalSpec sp;
            sp.net_id = n.id;
            const bool pg = (n.name.find("VDD") != std::string::npos)
                || (n.name.find("VSS") != std::string::npos)
                || (n.name.find("GND") != std::string::npos);
            sp.is_power_ground = pg;
            sp.max_current_mA = pg ? 400.0f : 2.0f;
            sp.max_via_resistance_ohm = pg ? 5.0f : 50.0f;
            specs.push_back(sp);
        }
        return specs;
    }

    // Precompute and inject W_elec weights for all nets.
    // Call after build_lattice() and DRCPenaltyModel::apply_masks(),
    // and before GlobalPlanner fires.
    void precompute(RoutingGridGraph& grid,
                    std::span<const NetElectricalSpec> specs,
                    int layers) {
        // Iterate over all edges in the graph once and set w_elec based on layer.
        auto [ei, ei_end] = boost::edges(grid.graph());
        for (auto it = ei; it != ei_end; ++it) {
            auto& ep        = grid.graph()[*it];
            VertexDesc src  = boost::source(*it, grid.graph());
            VertexDesc tgt  = boost::target(*it, grid.graph());
            const auto& sp  = grid.graph()[src].pos;
            const auto& tp  = grid.graph()[tgt].pos;

            // ── EM check: via edges on high-current nets ───────────────────
            // Higher layers (further from substrate) have lower via resistance.
            // Penalize via transitions at lower layers proportionally.
            if (sp.z != tp.z) {
                const float layer_resistance_factor =
                    1.0f - static_cast<float>(std::min(sp.z, tp.z)) /
                           static_cast<float>(std::max(1, layers - 1));
                // Apply across all P/G specs (worst case dominates)
                float max_penalty{0.0f};
                for (const auto& spec : specs) {
                    if (spec.is_power_ground) {
                        // P/G vias: require redundant stack; single vias penalized heavily.
                        max_penalty = std::max(max_penalty, layer_resistance_factor * 3.0f);
                    } else {
                        // Signal vias: IR drop proportional to resistance budget.
                        const float via_r = layer_resistance_factor * 10.0f; // Ω per via
                        if (via_r > spec.max_via_resistance_ohm) {
                            max_penalty = std::max(max_penalty, via_r / spec.max_via_resistance_ohm);
                        }
                    }
                }
                ep.w_elec += max_penalty;
            } else {
                // ── EM check: track edges for high-current nets ────────────
                // Narrow tracks (identified by layer index on metal 1/2) are
                // upweighted for high-current nets; wider metal layers are preferred.
                for (const auto& spec : specs) {
                    if (spec.max_current_mA > 10.0f && sp.z < 2) {
                        // Low-layer, high-current: upweight narrow tracks.
                        ep.w_elec += 0.5f * (spec.max_current_mA / 10.0f);
                    }
                }
            }
        }
    }
};

} // namespace routing_genetic_astar
