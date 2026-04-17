#pragma once
#include <queue>
#include <unordered_set>
#include <vector>
#include <cmath>
#include <optional>
#include <boost/pool/object_pool.hpp>
#include "routing_genetic_astar/grid_graph.hpp"
#include "routing_genetic_astar/analysis/pin_access_oracle.hpp"

namespace routing_genetic_astar {

// ── A* node: lives inside boost::object_pool, never on the free store ─────────
// [The Whiteboard Analogy]: object_pool is a pre-allocated slab of memory.  When
// A* pops a node it simply resets the pointer — no OS malloc/free, no heap
// fragmentation, O(1) mass-reclamation when the pool destructor fires.
struct AStarNode {
    VertexDesc  vertex;
    float       g_cost{0.0f};   // Cost from source to this node
    float       h_cost{0.0f};   // Heuristic estimate to destination
    AStarNode*  parent{nullptr};

    AStarNode(VertexDesc v, float g, float h, AStarNode* p)
        : vertex(v), g_cost(g), h_cost(h), parent(p) {}
    [[nodiscard]] float f() const noexcept { return g_cost + h_cost; }
};

struct AStarNodeCmp {
    bool operator()(const AStarNode* a, const AStarNode* b) const noexcept {
        if (std::abs(a->f() - b->f()) < 1e-6f) return a->h_cost > b->h_cost;
        return a->f() > b->f(); // Min-heap: lowest F at top
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// DetailedGridRouter — Section 4.7 and Section 5 of architecture_v3.md
//
// Executes constrained A* within a GA-provided BoundingBox.
// Three gating mechanisms applied on every neighbor expansion:
//   1. Corridor clamp: skip vertices outside the assigned bbox.
//   2. DRC hard gate: skip edges where EdgeProperties::is_drc_blocked() is true.
//   3. PinAccessOracle: terminal expansion restricted to legal approach vectors.
//
// Edge cost = EdgeProperties::effective_weight() (w_base + w_cong + w_elec).
// After routing, calls RoutingGridGraph::try_claim_edge() to commit the path.
// ═══════════════════════════════════════════════════════════════════════════════
class DetailedGridRouter {
public:
    // Route a single net (2-terminal) within an optional bounding box.
    // src_pin / dst_pin are indices into the net's pin list (for PinAccessOracle).
    // Returns the list of VertexDesc forming the path (empty on failure).
    [[nodiscard]] std::vector<VertexDesc>
    route_net(int net_id, int src_pin_idx, int dst_pin_idx,
              VertexDesc src, VertexDesc dst,
              RoutingGridGraph& grid,
              const PinAccessOracle& pao,
              const std::optional<BoundingBox>& bbox = std::nullopt) {
        // [The Whiteboard Analogy]: object_pool pre-allocates a slab of AStarNode memory.
        // pool.malloc() grabs a raw slot from the slab — O(1), no heap fragmentation.
        // placement-new ("new (slot) T(...)") constructs the object in that slot in-place.
        // pool.destroy() or pool destructor reclaims ALL nodes in O(1) at end of routing.
        // NOTE: Boost 1.82+ removed pool.construct(); use malloc() + placement-new instead.
        boost::object_pool<AStarNode> pool;

        std::priority_queue<AStarNode*, std::vector<AStarNode*>, AStarNodeCmp> open_set;
        std::unordered_set<VertexDesc> closed;

        void* slot = pool.malloc();
        AStarNode* start = new (slot) AStarNode(src, 0.0f, heuristic(src, dst, grid), nullptr);
        open_set.push(start);

        // Fetch legal terminal neighbors for the destination pin (PinAccessOracle gating).
        auto legal_dst_opt = pao.legal_terminals(net_id, dst_pin_idx);

        while (!open_set.empty()) {
            AStarNode* curr = open_set.top(); open_set.pop();

            if (closed.contains(curr->vertex)) continue;
            closed.insert(curr->vertex);

            // Goal reached: reconstruct path.
            if (curr->vertex == dst) {
                std::vector<VertexDesc> path;
                for (AStarNode* n = curr; n; n = n->parent)
                    path.push_back(n->vertex);
                std::reverse(path.begin(), path.end());
                // Commit path to graph by claiming edges.
                commit_path(path, net_id, grid);
                return path;
            }

            // Expand neighbors.
            auto [out_b, out_e] = boost::out_edges(curr->vertex, grid.graph());
            for (auto eit = out_b; eit != out_e; ++eit) {
                const EdgeDesc e = *eit;
                const auto&    ep = grid.graph()[e];

                // Skip if another net owns this edge (conflict) and it's not free.
                if (ep.net_owner != -1 && ep.net_owner != net_id) continue;

                VertexDesc nbr = boost::target(e, grid.graph());

                // 1. Corridor clamp (bbox gate).
                if (bbox) {
                    const auto& vp = grid.graph()[nbr].pos;
                    if (!bbox->contains(vp.x, vp.y, vp.z)) [[unlikely]] continue;
                }

                // 2. DRC hard gate — compute direction flag for this move.
                const uint32_t dir = direction_flag(curr->vertex, nbr, grid);
                if (ep.is_drc_blocked(dir)) [[unlikely]] continue;

                // 3. PinAccessOracle terminal gating: if nbr == dst, it must be
                //    reachable via a legal approach vector.
                if (nbr == dst && legal_dst_opt) {
                    const auto& legal = *legal_dst_opt;
                    bool legal_approach = false;
                    for (VertexDesc lv : legal) {
                        if (lv == curr->vertex) { legal_approach = true; break; }
                    }
                    if (!legal_approach) continue;
                }

                if (closed.contains(nbr)) continue;

                // [Train Switch Analogy - [[likely]]]: In the vast majority of expansions
                // the vertex is in-bounds and the edge is clean.  Marking the push branch
                // [[likely]] hints the branch predictor to keep the fast path hot in L1.
                float g_new = curr->g_cost + ep.effective_weight();
                void* slot2 = pool.malloc();
                AStarNode* nbr_node = new (slot2) AStarNode(
                    nbr, g_new, heuristic(nbr, dst, grid), curr);
                open_set.push(nbr_node);
            }
        }

        return {}; // Routing failed: no path found within corridor.
    }

private:
    // Manhattan distance heuristic in 3D (layer transitions cost 5× a track move).
    [[nodiscard]] float heuristic(VertexDesc a, VertexDesc b,
                                   const RoutingGridGraph& grid) const noexcept {
        const auto& pa = grid.graph()[a].pos;
        const auto& pb = grid.graph()[b].pos;
        return static_cast<float>(std::abs(pa.x - pb.x) +
                                  std::abs(pa.y - pb.y) +
                                  std::abs(pa.z - pb.z) * 5);
    }

    // Geometric direction flag for DRC gate.
    [[nodiscard]] uint32_t direction_flag(VertexDesc from, VertexDesc to,
                                          const RoutingGridGraph& grid) const noexcept {
        const auto& fp = grid.graph()[from].pos;
        const auto& tp = grid.graph()[to].pos;
        if (tp.z > fp.z) return MASK_VIA_UP;
        if (tp.z < fp.z) return MASK_VIA_DOWN;
        if (tp.x > fp.x) return MASK_EAST;
        if (tp.x < fp.x) return MASK_WEST;
        if (tp.y > fp.y) return MASK_NORTH;
        return MASK_SOUTH;
    }

    // Claim all edges along the found path in the RoutingGridGraph.
    void commit_path(const std::vector<VertexDesc>& path, int net_id,
                     RoutingGridGraph& grid) {
        for (size_t i = 1; i < path.size(); ++i) {
            auto opt_e = grid.edge_between(path[i-1], path[i]);
            if (opt_e) (void)grid.try_claim_edge(*opt_e, net_id);
        }
    }
};

} // namespace routing_genetic_astar
