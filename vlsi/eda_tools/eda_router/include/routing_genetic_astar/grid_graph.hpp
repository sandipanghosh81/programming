#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// FILE: grid_graph.hpp  —  The Physical Routing Fabric
// ═══════════════════════════════════════════════════════════════════════════════
//
// WHAT THIS FILE DOES:
//   Builds and manages the entire 3D routing lattice — the mathematical object
//   that represents every legal track and via on the chip.  Every other routing
//   component reads or writes data INTO this graph.
//
// HIGH-LEVEL ANALOGY:
//   Think of this as a 3D city map where:
//     - Vertices (intersections) = points on the routing grid (x, y, metal-layer)
//     - Edges (roads)            = legal routing moves between adjacent grid points
//     - Edge properties          = everything about that road segment:
//         w_base        → speed limit (base travel cost)
//         w_cong_history → current traffic jam (congestion history)
//         drc_mask      → road-closed signs (geometric rule violations)
//         w_elec        → weight limit posted (electrical restrictions)
//         net_owner     → which delivery truck currently owns this road segment
//         frozen        → road permanently reserved (ECO frozen net)
//
// ─────────────────────────────────────────────────────────────────────────────
// KEY DEPENDENCY: Boost.Graph (boost::adjacency_list)
//
// WHY BOOST.GRAPH INSTEAD OF A CUSTOM GRAPH?
//   Boost.Graph is a production-quality, heavily tested C++ graph library.
//   It provides:
//     - A variety of graph representation options (adjacency list, matrix, etc.)
//     - A rich algorithm library (BFS, DFS, Dijkstra, Bellman-Ford, etc.)
//     - Generic programming: algorithms work with ANY graph type via C++ concepts
//   Rolling our own graph would take months and introduce bugs Boost has
//   already fixed over 20+ years.
//
// ─── BOOST FEATURE: adjacency_list ───────────────────────────────────────────
// boost::adjacency_list<OutEdgeList, VertexList, Directed, VertexProp, EdgeProp>
//
//   OutEdgeList = boost::vecS → the list of outgoing edges per vertex is stored
//     in a std::vector.  ANALOGY: Each intersection has a plain array of roads.
//     vecS gives O(1) amortised edge addition and fast iteration.
//
//   VertexList  = boost::vecS → all vertices are stored in one flat std::vector.
//     ANALOGY: [THE ROLODEX]: All employee cards stacked sequentially in one
//     tray.  You can grab card #47 by counting to position 47 — O(1) lookup.
//     This is DRAMATICALLY faster than std::list (which would require counting
//     from card #1 every time) or std::map (which follows tree pointers).
//     CPU caches love sequentially stored data — entire rows of adjacent vertices
//     fit in a single 64-byte cache line.
//
//   undirectedS → edges are bidirectional (routing goes both ways on a track).
//
//   VertexProp  = VertexProperties → our custom data bundle per vertex.
//   EdgeProp    = EdgeProperties   → our custom data bundle per edge.
//
// ─── BOOST FEATURE: flat_map from boost::container ───────────────────────────
// boost::container::flat_map<Key, Value> stores key-value pairs sorted in a
// flat std::vector rather than a red-black tree.
//
// ANALOGY: A physical Rolodex of sorted index cards.  To find "Smith", you
// can do binary search through card #1, #14, #27, ... touching cards that are
// physically next to each other in memory.  A std::map (red-black tree) would
// instead follow pointer-chains to scattered heap nodes — much slower for
// CPU cache because each "next" card could be anywhere in RAM.
//
// flat_map tradeoff: O(log n) reads (same as std::map), but O(n) insert/erase.
// For the CongestionOracle which is built once per pass and read millions of
// times, this tradeoff is strongly positive.
// ─────────────────────────────────────────────────────────────────────────────

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/container/flat_map.hpp>

#include <vector>
#include <optional>
#include <cstdint>
#include <algorithm>
#include "routing_genetic_astar/types.hpp"

namespace routing_genetic_astar {

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 1: DRC Direction Bitmask Constants
// ─────────────────────────────────────────────────────────────────────────────
//
// WHAT THESE ARE:
//   Each constant is a single bit in a 32-bit number.  The DRCPenaltyModel sets
//   these bits on edges where expanding in a specific direction would violate a
//   manufacturing rule.  DetailedGridRouter checks these bits BEFORE moving.
//
// WHY BITMASKS INSTEAD OF BOOLEANS?
//   Six individual bool fields would take 6 bytes and require 6 comparisons.
//   A single uint32_t bitmask checks all six directions in ONE bitwise AND:
//     if (mask & MASK_NORTH) → blocked going North
//   ANALOGY: A traffic light with 6 colored bulbs in one fixture, vs. 6
//   separate single-color lights.  One fixture, one glance.
//
// ─── C++ FEATURE: inline constexpr ───────────────────────────────────────────
// `constexpr` = the value is computed at COMPILE TIME, not at runtime.
//   ANALOGY: The value is carved in stone before the program even runs.
//   No memory is allocated at runtime for this constant.
// `inline` = this definition can appear in multiple translation units without
//   linker errors (required when constexpr variables are defined in headers).
// ─────────────────────────────────────────────────────────────────────────────
inline constexpr uint32_t MASK_EAST     = 0x01u; // Block eastward  move (x+1)
inline constexpr uint32_t MASK_WEST     = 0x02u; // Block westward  move (x-1)
inline constexpr uint32_t MASK_NORTH    = 0x04u; // Block northward move (y+1)
inline constexpr uint32_t MASK_SOUTH    = 0x08u; // Block southward move (y-1)
inline constexpr uint32_t MASK_VIA_UP   = 0x10u; // Block via transition upward   (z+1)
inline constexpr uint32_t MASK_VIA_DOWN = 0x20u; // Block via transition downward (z-1)

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 2: EdgeProperties — The Four-Channel Weight Model
// ─────────────────────────────────────────────────────────────────────────────
//
// WHAT THIS IS:
//   The per-edge data bundle that Boost.Graph stores on every routing edge.
//   This IS the entire weight model from Section 4.7 of architecture_v3.md.
//
// THE FOUR INDEPENDENT CHANNELS (critical: they DO NOT mix):
//
//   CHANNEL 1 — w_base:
//     Set once during build_lattice().  Never changes.
//     Represents the inherent preference for this edge: lower layers have higher
//     congestion so they cost slightly more; via transitions cost more than
//     single-layer moves.
//     ANALOGY: The posted speed limit on a road — fixed by the road designers.
//
//   CHANNEL 2 — w_cong_history:
//     Incremented by HistoryCostUpdater after every NRL pass where this edge
//     was contested.  Never decremented (persistent PathFinder history).
//     This is the "memory" that steers nets away from chronically congested edges.
//     ANALOGY: A GPS app that remembers which intersections are ALWAYS jammed
//     at rush hour and permanently adds a detour penalty.
//
//   CHANNEL 3 — drc_mask:
//     Bitmask set ONCE by DRCPenaltyModel before routing starts.
//     Direction-specific HARD BLOCKS — not a cost, an absolute prohibition.
//     A bit set → A* CANNOT expand through this edge in that direction.
//     ANALOGY: "Road Closed" signs for specific approach angles.
//
//   CHANNEL 4 — w_elec:
//     Set ONCE by ElectricalConstraintEngine before routing starts.
//     Upweights edges that would carry too much current or create too much
//     resistance for the net assigned to them.
//     ANALOGY: Weight limit signs ("Max 5 tons") on bridges.  Heavy-current
//     nets are steered toward bridges rated for their load.
//
// WHY KEEP THEM SEPARATE?
//   In v2 of this engine, DRC violations were just added to w_cong.  This was
//   a bug: if congestion was high, DRC violations were "drowned out" (relative
//   to congestion they looked cheap).  Separating the channels means DRC masks
//   are ALWAYS enforced regardless of congestion history.
// ─────────────────────────────────────────────────────────────────────────────
struct EdgeProperties {

    // ── Channel 1: Base cost (set at lattice construction time) ──────────────
    float    w_base{1.0f};

    // ── Channel 2: Congestion history (incremented per NRL conflict) ─────────
    float    w_cong_history{0.0f};

    // ── Channel 3: DRC direction-disable bitmask ──────────────────────────────
    uint32_t drc_mask{0u};

    // ── Channel 4: EM/IR electrical weight (set by ElectricalConstraintEngine) ─
    float    w_elec{0.0f};

    // ── Ownership (PathFinder conflict detection) ─────────────────────────────
    // -1 = free, ≥0 = net_id that claimed this edge
    // NOTE: In a production system this would be std::atomic<int32_t> in a
    // parallel array indexed by edge_index for true lockless CAS.  We use
    // int32_t here for clarity; our try_claim_edge() serializes access.
    int32_t  net_owner{-1};

    // ── ECO frozen mask ────────────────────────────────────────────────────────
    // When true, this edge contributes to capacity as an obstruction but cannot
    // be ripped up by NegotiatedRoutingLoop (EcoRouter sets this for preserved nets).
    bool     frozen{false};

    // ── FUNCTION: effective_weight ─────────────────────────────────────────────
    // WHAT IT DOES:
    //   Computes the A* edge traversal cost seen by DetailedGridRouter.
    //   This is the formula from Section 4.7 of architecture_v3.md:
    //     cost = W_base + W_cong×history + W_elec×electrical_adjustment
    //
    // IMPORTANT: drc_mask is NOT included here.  DRC is a hard GATE checked
    // in DetailedGridRouter BEFORE the cost is even computed for blocked directions.
    //
    // ─── C++ FEATURE: [[nodiscard]] ──────────────────────────────────────────
    // Marking this [[nodiscard]] forces the compiler to warn if you call
    // effective_weight() but do not USE the returned float.
    // EXAMPLE OF THE BUG THIS PREVENTS:
    //   edge.effective_weight();  // Did nothing — forgot to use the value!
    //   float cost = edge.effective_weight();  // Correct
    //
    // ─── C++ FEATURE: noexcept ───────────────────────────────────────────────
    // This function NEVER throws exceptions.  The compiler can generate faster
    // call sites because no exception-handling bookkeeping is needed.
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] float effective_weight() const noexcept {
        return w_base + w_cong_history + w_elec;
    }

    // ── FUNCTION: is_drc_blocked ───────────────────────────────────────────────
    // WHAT IT DOES:
    //   Checks if expanding in the given direction is prohibited by a DRC rule.
    //   Called by DetailedGridRouter on EVERY neighbor expansion — must be fast.
    //
    // PARAMETER `dir`: one of the MASK_EAST / MASK_NORTH / MASK_VIA_UP etc.
    //   constants defined above.  The function uses bitwise AND to check if
    //   that specific direction bit is set in drc_mask.
    //
    // EXAMPLE:
    //   edge.drc_mask = MASK_NORTH | MASK_VIA_UP  (binary: 0b010100)
    //   edge.is_drc_blocked(MASK_NORTH)   → true  (bit 2 is set)
    //   edge.is_drc_blocked(MASK_EAST)    → false (bit 0 not set)
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] bool is_drc_blocked(uint32_t dir) const noexcept {
        return (drc_mask & dir) != 0u;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 3: VertexProperties — Per-Grid-Point Data
// ─────────────────────────────────────────────────────────────────────────────
struct VertexProperties {
    GridPoint pos{};               // 3D coordinate of this grid point
    float     gcell_demand{0.0f};  // How many nets are currently using this cell
    float     gcell_capacity{1.0f};// How many nets this cell can safely hold
    bool      is_ghost{false};     // If true: SpatialPartitioner boundary cell
                                   //   (read-only for non-owner threads)
};

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 4: The Boost Graph Type Aliases
// ─────────────────────────────────────────────────────────────────────────────
//
// WHY TYPE ALIASES?
//   boost::adjacency_list<vecS, vecS, undirectedS, VertexProperties, EdgeProperties>
//   is very long to type repeatedly.  Using `using RoutingGraph = ...` gives
//   it a short, meaningful name everywhere else.
//   ANALOGY: Naming a street "Main St." instead of "the big road that goes from
//   the library past the park and ends at the fire station".
// ─────────────────────────────────────────────────────────────────────────────

using RoutingGraph = boost::adjacency_list<
    boost::vecS,        // OutEdge storage: flat vector per vertex (cache-friendly)
    boost::vecS,        // Vertex storage:  flat vector, O(1) integer index lookup
    boost::undirectedS, // Bidirectional routing moves
    VertexProperties,   // Data attached to each vertex
    EdgeProperties      // Data attached to each edge (the weight model)
>;

// ─── C++ FEATURE: using aliases for graph descriptor types ───────────────────
// When you use boost::vecS as the vertex storage, vertex_descriptor is just
// a size_t (an integer index into the vertex vector).  This makes it very fast:
// accessing vertex 42 is just graph.vertex_array[42] — one array dereference.
// ─────────────────────────────────────────────────────────────────────────────
using VertexDesc = boost::graph_traits<RoutingGraph>::vertex_descriptor;
using EdgeDesc   = boost::graph_traits<RoutingGraph>::edge_descriptor;

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 5: LayerConfig — Per-Metal-Layer Routing Parameters
// ─────────────────────────────────────────────────────────────────────────────
struct LayerConfig {
    // Base weight for horizontal/vertical track edges on this layer.
    // CONVENTION: Even layers (M1, M3, M5) prefer horizontal routing (lower h-cost).
    //             Odd layers (M2, M4, M6) prefer vertical routing (lower v-cost).
    // The preferred direction gets weight 1.0, non-preferred gets 1.2.
    float preferred_track_weight{1.0f};

    // Extra cost of transitioning between this layer and the NEXT layer via a via.
    // Via transitions are 2–5× more expensive than track moves because vias:
    //   1. Consume physical via landing area
    //   2. Add resistance (IR drop concern)
    //   3. Must satisfy via enclosure DRC rules
    float via_penalty{2.0f};
};

// ═══════════════════════════════════════════════════════════════════════════════
// CLASS: RoutingGridGraph
// ═══════════════════════════════════════════════════════════════════════════════
//
// WHAT THIS CLASS IS:
//   The central shared data structure for the entire routing engine.
//   All components — GlobalPlanner, DetailedGridRouter, HistoryCostUpdater,
//   DRCPenaltyModel, ElectricalConstraintEngine, NegotiatedRoutingLoop — read
//   from and write to this one object.
//
// TWO-LEVEL ABSTRACTION:
//   MACRO LEVEL: GCell demand/capacity on each vertex.
//     Used by GlobalPlanner fitness function: "is this area over-capacity?"
//     Used by CongestionOracle: "how crowded is this region?"
//
//   MICRO LEVEL: Full 3D track lattice at manufacturing pitch.
//     Used by DetailedGridRouter A*: "which specific track segment should I use?"
//     Used by HistoryCostUpdater: "which edge had a conflict?"
//
// THREAD SAFETY:
//   During NegotiatedRoutingLoop parallel execution, only SpatialPartitioner-
//   assigned vertices are written by each thread.  Ghost-cell edges are
//   written ONLY by CrossRegionMediator (serialized by std::barrier).
//   try_claim_edge() is called from within each thread's own region only
//   (no two threads own the same vertex), so mutex overhead is avoided.
// ═══════════════════════════════════════════════════════════════════════════════
class RoutingGridGraph {
public:
    RoutingGridGraph() = default;

    // ── FUNCTION: build_lattice ────────────────────────────────────────────────
    // ─────────────────────────────────────────────────────────────────────────
    // WHAT IT DOES (step by step):
    //
    //   STEP 1: Pre-allocate the vertex lookup table (vertex_lut_).
    //     Creates rows×cols×layers vertex descriptors and stores them in a flat
    //     vector.  The index formula: vertex_at(x,y,z) = vertex_lut_[x + y*cols + z*rows*cols]
    //     gives O(1) lookup of any 3D coordinate without any hash or search.
    //
    //   STEP 2: Add all vertices to the Boost graph.
    //     boost::add_vertex(graph) returns a VertexDesc (integer index).
    //     We store x/y/layer and initialize gcell_capacity from the layer config.
    //
    //   STEP 3: Add horizontal track edges.
    //     For each layer, connects each vertex to its East neighbor.
    //     w_base = layerConfig.preferred_track_weight
    //
    //   STEP 4: Add vertical track edges.
    //     For each layer, connects each vertex to its North neighbor.
    //
    //   STEP 5: Add via edges (vertical layer transitions).
    //     For each (x,y) position, connects layer L to layer L+1.
    //     w_base = layerConfig.via_penalty
    //
    // TOTAL EDGES CREATED for a R×C×L grid:
    //   Horizontal: L × R × (C-1)
    //   Vertical:   L × (R-1) × C
    //   Via:        (L-1) × R × C
    //   For a 100×100×6 grid: 6×100×99 + 6×99×100 + 5×100×100 = 168,800 edges
    //
    // ANALOGY: Building a city street map layer by layer, then installing
    // elevator shafts (vias) between each floor pair.
    // ─────────────────────────────────────────────────────────────────────────
    void build_lattice(int rows, int cols, int layers,
                       const std::vector<LayerConfig>& layer_cfg) {
        rows_ = rows; cols_ = cols; layers_ = layers;
        vertex_lut_.resize(static_cast<size_t>(rows * cols * layers));
        add_all_vertices(rows, cols, layers, layer_cfg);
        add_horizontal_edges(rows, cols, layers, layer_cfg);
        add_vertical_edges(rows, cols, layers, layer_cfg);
        add_via_edges(rows, cols, layers, layer_cfg);
    }

    // ── FUNCTION: vertex_at (two overloads) ────────────────────────────────────
    // WHAT IT DOES:
    //   Returns the Boost VertexDesc (an integer index) for a given 3D coordinate.
    //   O(1) lookup via the pre-built vertex_lut_ flat array.
    //
    // ─── C++ FEATURE: [[nodiscard]] ──────────────────────────────────────────
    // If you call vertex_at(1,2,0) and don't use the VertexDesc you get back,
    // the compiler will warn you.  This catches:
    //    graph.vertex_at(x, y, l);  // BUG: returned vertex immediately discarded!
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] VertexDesc vertex_at(int x, int y, int layer) const {
        return vertex_lut_[static_cast<size_t>(index_of(x, y, layer))];
    }
    [[nodiscard]] VertexDesc vertex_at(const GridPoint& p) const {
        return vertex_lut_[static_cast<size_t>(index_of(p.x, p.y, p.z))];
    }

    // ── FUNCTION: edge_between ─────────────────────────────────────────────────
    // WHAT IT DOES:
    //   Asks Boost if an edge exists between vertices u and v.
    //   Returns the EdgeDesc wrapped in std::optional.
    //
    // ─── C++ FEATURE: std::optional ──────────────────────────────────────────
    // std::optional<T> is a container that holds EITHER a value of type T OR
    // nothing at all (std::nullopt).
    //
    // ANALOGY: A "Maybe Box" (like a Schrödinger's gift box).
    //   - optional<EdgeDesc> with a value: the box contains an EdgeDesc
    //   - optional<EdgeDesc> with nullopt: the box is empty
    //
    // WHY NOT RETURN -1 AS A SENTINEL?
    //   Old C code returned -1, nullptr, or 0 to mean "nothing".  The problem:
    //   you had to READ THE DOCUMENTATION to know -1 was the sentinel value,
    //   and nothing stopped you from accidentally using -1 as a real value.
    //
    //   std::optional is SELF-DOCUMENTING: the type signature says
    //   "this function might not give you an edge".  You MUST call `.has_value()`
    //   or use `if (opt)` before accessing the value — the compiler forces you.
    //
    // USAGE:
    //   auto opt_edge = graph.edge_between(u, v);
    //   if (opt_edge) {            // Check if it has a value
    //       use(*opt_edge);        // Safe: we know it exists
    //   }
    //   or: opt_edge.value_or(default_edge)  // Provide a fallback
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] std::optional<EdgeDesc> edge_between(VertexDesc u, VertexDesc v) const {
        // boost::edge() returns a pair<EdgeDesc, bool>
        // ─── C++ FEATURE: Structured Bindings (C++17) ────────────────────────
        // `auto [ed, found] = boost::edge(u, v, graph_);`
        // Instead of writing:
        //   auto result = boost::edge(u, v, graph_);
        //   auto ed = result.first;
        //   bool found = result.second;
        // We "unpack" (decompose) the pair into named variables in ONE line.
        // ANALOGY: Opening a package and labelling each item as it comes out.
        // ─────────────────────────────────────────────────────────────────────
        auto [ed, found] = boost::edge(u, v, graph_);
        if (!found) return std::nullopt; // Box is empty
        return ed;                        // Box contains the EdgeDesc
    }

    // ── FUNCTION: try_claim_edge ───────────────────────────────────────────────
    // WHAT IT DOES:
    //   Attempts to assign a routing edge to a net (net_id).
    //   Returns true  if the edge was free and is now owned by net_id.
    //   Returns false if the edge was already owned (CONFLICT detected).
    //
    // ANALOGY: A parking spot with a "RESERVED — Fleet Vehicle #7" sign.
    //   If the spot is empty, you paint your truck number on it.
    //   If another truck is already there, you drive away and report a conflict.
    //
    // HOW CONFLICTS ARE DETECTED IN NRL:
    //   NegotiatedRoutingLoop calls this for every edge in a route.
    //   If it returns false, the net is added to the "conflicted" list and
    //   its history cost is bumped so it routes differently next pass.
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] bool try_claim_edge(EdgeDesc e, int net_id) {
        auto& ep = graph_[e];
        if (ep.frozen || ep.net_owner != -1) return false; // Spot taken / reserved
        ep.net_owner = net_id;
        graph_[boost::source(e, graph_)].gcell_demand += 1.0f;
        graph_[boost::target(e, graph_)].gcell_demand += 1.0f;
        return true; // Successfully claimed
    }

    // ── FUNCTION: release_net ──────────────────────────────────────────────────
    // WHAT IT DOES:
    //   "Rip up" — removes all edges owned by net_id from the graph so this
    //   net can be re-routed in the next NRL pass.  Frozen edges are skipped.
    //
    // ANALOGY: A parking inspector going through the lot and erasing all "Truck #7"
    //   markings, leaving those spots free for re-assignment in the next shift.
    //
    // PERFORMANCE NOTE: This iterates over all graph edges.
    //   For a 100×100×6 grid ~170k edges.  Called per-conflict-net per pass.
    //   Acceptable for up to ~100k nets; production systems would maintain a
    //   per-net edge list for O(path_length) rip-up.
    // ─────────────────────────────────────────────────────────────────────────
    void release_net(int net_id) {
        auto [ei, ei_end] = boost::edges(graph_);
        for (auto it = ei; it != ei_end; ++it) {
            auto& ep = graph_[*it];
            if (ep.net_owner != net_id || ep.frozen) continue;
            ep.net_owner = -1;
            // Reduce demand at both endpoints of this edge.
            auto& sv = graph_[boost::source(*it, graph_)];
            auto& tv = graph_[boost::target(*it, graph_)];
            sv.gcell_demand = std::max(0.0f, sv.gcell_demand - 1.0f);
            tv.gcell_demand = std::max(0.0f, tv.gcell_demand - 1.0f);
        }
    }

    // ── FUNCTION: freeze_net ────────────────────────────────────────────────────
    // WHAT IT DOES (ECO flows):
    //   Marks all edges owned by net_id as "frozen".  Frozen edges block routing
    //   attempts by other nets but cannot be ripped up.
    //   Used by EcoRouter to preserve existing routes during change-only re-routing.
    //
    // ANALOGY: Spray-painting "PERMANENT RESERVATION — DO NOT MOVE" on a parking
    //   spot.  The spot is taken, but it's also untouchable by other operations.
    // ─────────────────────────────────────────────────────────────────────────
    void freeze_net(int net_id) {
        auto [ei, ei_end] = boost::edges(graph_);
        for (auto it = ei; it != ei_end; ++it)
            if (graph_[*it].net_owner == net_id)
                graph_[*it].frozen = true;
    }

    // ── FUNCTION: mark_ghost ────────────────────────────────────────────────────
    // WHAT IT DOES:
    //   Marks a vertex as a "ghost cell" — a boundary cell between two SpatialPartitioner
    //   regions.  Ghost cells are readable by both adjacent thread regions but
    //   writable ONLY by CrossRegionMediator (after std::barrier sync).
    // ─────────────────────────────────────────────────────────────────────────
    void mark_ghost(VertexDesc v) { graph_[v].is_ghost = true; }

    // ── FUNCTION: gcell_overflow ────────────────────────────────────────────────
    // WHAT IT DOES:
    //   Returns how far over-capacity this vertex (GCell) is.
    //   Returns 0.0f if the cell is under-capacity (no overflow).
    //   Positive values indicate routing congestion — the GA tries to minimize
    //   the SQUARED sum of these values across all vertices.
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] float gcell_overflow(VertexDesc v) const noexcept {
        const auto& vp = graph_[v];
        return std::max(0.0f, vp.gcell_demand - vp.gcell_capacity);
    }

    // ── FUNCTION: total_overflow ────────────────────────────────────────────────
    // WHAT IT DOES:
    //   Sums overflow across all vertices.  Used as part of the GlobalPlanner
    //   fitness function.  Lower total overflow = better routing plan.
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] float total_overflow() const noexcept {
        float sum{0.0f};
        auto [vi, vi_end] = boost::vertices(graph_);
        for (auto it = vi; it != vi_end; ++it)
            sum += gcell_overflow(*it);
        return sum;
    }

    // ── Raw graph access ────────────────────────────────────────────────────────
    // Provided for DRCPenaltyModel, ElectricalConstraintEngine, and
    // algorithm traversal code that needs direct iterator access.
    [[nodiscard]] const RoutingGraph& graph() const noexcept { return graph_; }
    [[nodiscard]]       RoutingGraph& graph()       noexcept { return graph_; }

    // ── Grid dimension accessors ─────────────────────────────────────────────────
    [[nodiscard]] int rows()   const noexcept { return rows_; }
    [[nodiscard]] int cols()   const noexcept { return cols_; }
    [[nodiscard]] int layers() const noexcept { return layers_; }

    // ── FUNCTION: in_bounds ─────────────────────────────────────────────────────
    // WHAT IT DOES:
    //   Returns true if (x, y, layer) is a valid grid coordinate — inside the
    //   allocated lattice.  Used by CorridorRefinement, DetailedGridRouter,
    //   and SpineFishboneRouter before calling vertex_at() to prevent out-of-bounds.
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] bool in_bounds(int x, int y, int layer) const noexcept {
        return x >= 0 && x < cols_ && y >= 0 && y < rows_
            && layer >= 0 && layer < layers_;
    }

private:
    // ─────────────────────────────────────────────────────────────────────────
    // PRIVATE HELPERS — lattice construction split into named sub-functions
    // for readability (each step in build_lattice is separately named below).
    // ─────────────────────────────────────────────────────────────────────────

    // STEP 1: Create all vertices and populate the O(1) lookup table.
    void add_all_vertices(int rows, int cols, int layers,
                          const std::vector<LayerConfig>& layer_cfg) {
        for (int l = 0; l < layers; ++l) {
            for (int y = 0; y < rows; ++y) {
                for (int x = 0; x < cols; ++x) {
                    VertexDesc v = boost::add_vertex(graph_);
                    graph_[v].pos            = GridPoint{x, y, l};
                    // gcell_capacity represents how many nets can share this cell:
                    // use the preferred_track_weight as a proxy (lower weight = wider road).
                    graph_[v].gcell_capacity = layer_cfg[static_cast<size_t>(l)].preferred_track_weight;
                    vertex_lut_[static_cast<size_t>(index_of(x, y, l))] = v;
                }
            }
        }
    }

    // STEP 2: Connect each vertex to its eastern neighbor (x → x+1 on same layer).
    void add_horizontal_edges(int rows, int cols, int layers,
                               const std::vector<LayerConfig>& layer_cfg) {
        for (int l = 0; l < layers; ++l) {
            const float w = layer_cfg[static_cast<size_t>(l)].preferred_track_weight;
            for (int y = 0; y < rows; ++y) {
                for (int x = 0; x < cols - 1; ++x) {
                    EdgeProperties ep; ep.w_base = w;
                    boost::add_edge(vertex_lut_[static_cast<size_t>(index_of(x,   y, l))],
                                    vertex_lut_[static_cast<size_t>(index_of(x+1, y, l))],
                                    ep, graph_);
                }
            }
        }
    }

    // STEP 3: Connect each vertex to its northern neighbor (y → y+1 on same layer).
    void add_vertical_edges(int rows, int cols, int layers,
                             const std::vector<LayerConfig>& layer_cfg) {
        for (int l = 0; l < layers; ++l) {
            const float w = layer_cfg[static_cast<size_t>(l)].preferred_track_weight;
            for (int y = 0; y < rows - 1; ++y) {
                for (int x = 0; x < cols; ++x) {
                    EdgeProperties ep; ep.w_base = w;
                    boost::add_edge(vertex_lut_[static_cast<size_t>(index_of(x, y,   l))],
                                    vertex_lut_[static_cast<size_t>(index_of(x, y+1, l))],
                                    ep, graph_);
                }
            }
        }
    }

    // STEP 4: Add via edges (layer l → layer l+1 for all (x,y)).
    void add_via_edges(int rows, int cols, int layers,
                       const std::vector<LayerConfig>& layer_cfg) {
        for (int l = 0; l < layers - 1; ++l) {
            const float vp = layer_cfg[static_cast<size_t>(l)].via_penalty;
            for (int y = 0; y < rows; ++y) {
                for (int x = 0; x < cols; ++x) {
                    EdgeProperties ep; ep.w_base = vp;
                    boost::add_edge(vertex_lut_[static_cast<size_t>(index_of(x, y, l))],
                                    vertex_lut_[static_cast<size_t>(index_of(x, y, l+1))],
                                    ep, graph_);
                }
            }
        }
    }

    // HELPER: Convert (x, y, layer) into the flat index for vertex_lut_.
    // Index formula: x + y*cols + layer*rows*cols
    // ANALOGY: Calculating a seat number in a multi-story theatre:
    //   seat = column + row × cols_per_row + floor × seats_per_floor
    [[nodiscard]] int index_of(int x, int y, int layer) const noexcept {
        return x + y * cols_ + layer * rows_ * cols_;
    }

    // ─── Private member variables ─────────────────────────────────────────────
    RoutingGraph            graph_;      // The Boost adjacency_list (the city map)
    std::vector<VertexDesc> vertex_lut_; // O(1) 3D → VertexDesc lookup table
    int rows_{0}, cols_{0}, layers_{0};  // Grid dimensions
};

} // namespace routing_genetic_astar
