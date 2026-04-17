#pragma once
#include <variant>
#include <type_traits>
#include "routing_genetic_astar/types.hpp"
#include "routing_genetic_astar/grid_graph.hpp"
#include "routing_genetic_astar/core/detailed_grid_router.hpp"
#include "routing_genetic_astar/routing/spine_fishbone_router.hpp"
#include "routing_genetic_astar/routing/tree_router.hpp"
#include "routing_genetic_astar/planner/grid_fill.hpp"
#include "routing_genetic_astar/planner/grid_fill_impl.hpp"
#include "routing_genetic_astar/analysis/pin_access_oracle.hpp"

namespace routing_genetic_astar {

// ═══════════════════════════════════════════════════════════════════════════════
// StrategyComposer — Section 4.6 and Section 5 of architecture_v3.md
//
// Selects routing topology per net based on RoutingContext + net properties.
// Central dispatcher: uses std::visit to decode the RoutingContext variant at
// compile time — zero virtual dispatch, zero dynamic_cast overhead.
//
// Dispatch table (Section 4.6 decision tree):
//   MEMORY_ARRAY  → GridFill (deterministic, no heuristic)
//   CLOCK_NETWORK → SpineFishboneRouter (H-tree), leaf → DetailedGridRouter
//   Multi-pin (>2 pins) → TreeRouter (Steiner), leaf → DetailedGridRouter
//   Single 2-pin       → DetailedGridRouter directly
//
// [The Swiss Army Knife Analogy]: std::variant<...> is the knife body.  std::visit
// is the motion of clicking the correct tool (blade/corkscrew/screwdriver) into place
// based on the current context — exactly one tool is deployed, compile-time safe.
// ═══════════════════════════════════════════════════════════════════════════════
class StrategyComposer {
public:
    // Route one net according to its context and topology.
    // Returns the vertices forming the routed path/tree segments.
    [[nodiscard]] std::vector<VertexDesc>
    compose_and_route(const RoutingContext& ctx,
                      const NetDefinition&  net,
                      RoutingGridGraph&      grid,
                      const PinAccessOracle& pao,
                      const std::optional<BoundingBox>& bbox = std::nullopt) {
        // [The Swiss Army Knife Analogy]: std::visit physically unwraps the variant
        // and dispatches to the exact lambda branch — no if/else chain, no RTTI.
        return std::visit([&](auto&& tag) -> std::vector<VertexDesc> {
            using T = std::decay_t<decltype(tag)>;

            // ── MEMORY_ARRAY: deterministic GridFill, no A* at all ──────────
            if constexpr (std::is_same_v<T, MemoryArrayTag>) {
                // GridFill produces a CorridorAssignment, not a path.
                // Return empty here — memory nets are stamped entirely by GridFill
                // in GlobalPlanner before StrategyComposer is even invoked.
                return {};
            }

            // ── CLOCK_NETWORK: H-tree spine + leaf DetailedGridRouter ────────
            else if constexpr (std::is_same_v<T, ClockNetworkTag>) {
                if (net.pins.size() < 2) return {};
                SpineFishboneRouter sfr;
                VertexDesc src = grid.vertex_at(net.pins[0]);
                std::vector<GridPoint> sinks(net.pins.begin() + 1, net.pins.end());
                return sfr.route_clock_tree(net.id, src, sinks, grid, pao, bbox);
            }

            // ── RANDOM_LOGIC or MIXED_SIGNAL: Steiner for multi-pin, A* for 2-pin ──
            else {
                if (net.pins.size() == 2) {
                    // Simple 2-pin route: direct DetailedGridRouter A*.
                    DetailedGridRouter router;
                    VertexDesc src = grid.vertex_at(net.pins[0]);
                    VertexDesc dst = grid.vertex_at(net.pins[1]);
                    return router.route_net(net.id, 0, 1, src, dst, grid, pao, bbox);
                } else {
                    // Multi-pin: Steiner tree decomposition via TreeRouter.
                    TreeRouter tr;
                    return tr.route_steiner(net.id, net.pins, grid, pao, bbox);
                }
            }
        }, ctx);
    }
};

} // namespace routing_genetic_astar
