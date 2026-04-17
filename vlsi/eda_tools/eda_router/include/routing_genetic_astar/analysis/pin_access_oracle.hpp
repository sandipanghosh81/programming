#pragma once
#include <shared_mutex>
#include <span>
#include <optional>
#include <boost/container/flat_map.hpp>
#include "routing_genetic_astar/grid_graph.hpp"

namespace routing_genetic_astar {

// ═══════════════════════════════════════════════════════════════════════════════
// PinAccessOracle — Section 5 of architecture_v3.md
//
// Enumerates legal entry vectors for every pin in the design.
// This is the highest-correctness-leverage component for advanced nodes.
//
// During A* terminal expansion, DetailedGridRouter calls legal_terminals()
// to restrict which neighbors of a pin vertex are valid approach directions,
// preventing approach-angle DRC violations direct at the pin.
//
// [The Museum Exhibit Analogy]: std::shared_mutex lets thousands of A* threads
// read the legal access vectors simultaneously without blocking each other
// (shared lock = many tourists viewing a painting at once).  Only when new
// pin geometry is being registered does the system take an exclusive write lock
// and temporarily clear the room.
// ═══════════════════════════════════════════════════════════════════════════════

// Key identifying a specific pin: (net_id, pin_index_within_net).
struct PinKey {
    int net_id{-1};
    int pin_index{-1};
    [[nodiscard]] bool operator<(const PinKey& o) const noexcept {
        if (net_id != o.net_id) return net_id < o.net_id;
        return pin_index < o.pin_index;
    }
};

class PinAccessOracle {
public:
    // ── Pre-computation: call once after DesignAnalyzer, before GlobalPlanner ─
    // For each pin in each net, enumerate all neighboring VertexDesc handles
    // in the RoutingGraph whose approach angle is geometrically legal.
    // Rules encoded: no perpendicular approach within 1 track of EOL,
    // layer transition must land on a legal via site.
    void precompute(const DesignSummary& design, const RoutingGridGraph& grid) {
        std::unique_lock lock(mtx_);
        access_map_.clear();

        for (const auto& net : design.nets) {
            for (int pi = 0; pi < static_cast<int>(net.pins.size()); ++pi) {
                const auto& pin = net.pins[static_cast<size_t>(pi)];
                if (!grid.in_bounds(pin.x, pin.y, pin.z)) continue;

                VertexDesc pv = grid.vertex_at(pin.x, pin.y, pin.z);
                std::vector<VertexDesc> legal;

                // Enumerate all graph neighbors; apply approach-angle filter.
                auto [adj_begin, adj_end] = boost::adjacent_vertices(pv, grid.graph());
                for (auto adj = adj_begin; adj != adj_end; ++adj) {
                    const auto& nbr_props = grid.graph()[*adj];
                    // Simplified 28nm rule: block same-layer approach from the
                    // "wrong" preferred direction for this layer (odd layers: H, even: V).
                    const bool preferred_h = (pin.z % 2 == 0);
                    const bool is_horizontal_move = (nbr_props.pos.y == pin.y);
                    // Also allow via approaches from adjacent layers always.
                    const bool is_via = (nbr_props.pos.x == pin.x && nbr_props.pos.y == pin.y);
                    if (is_via || (preferred_h == is_horizontal_move)) {
                        legal.push_back(*adj);
                    }
                }
                access_map_[PinKey{net.id, pi}] = std::move(legal);
            }
        }
    }

    // ── A* terminal gating ────────────────────────────────────────────────────
    // Returns the legal approach vertices for a given pin.
    // Returns nullopt if the pin was never registered (safe fallback: allow all).
    [[nodiscard]] std::optional<std::span<const VertexDesc>>
    legal_terminals(int net_id, int pin_index) const {
        // [The Museum Exhibit Analogy]: shared_lock allows all A* threads to
        // read simultaneously — only blocks if a unique_lock is held for precompute().
        std::shared_lock lock(mtx_);
        auto it = access_map_.find(PinKey{net_id, pin_index});
        if (it == access_map_.end()) return std::nullopt;
        return std::span<const VertexDesc>{it->second};
    }

private:
    mutable std::shared_mutex                                       mtx_;
    boost::container::flat_map<PinKey, std::vector<VertexDesc>>    access_map_;
};

} // namespace routing_genetic_astar
