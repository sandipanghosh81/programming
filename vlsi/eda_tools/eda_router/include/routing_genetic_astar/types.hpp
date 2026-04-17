#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// FILE: types.hpp  —  The Universal Data Dictionary
// ═══════════════════════════════════════════════════════════════════════════════
//
// WHAT THIS FILE IS:
//   This is the single source of truth for ALL data types used across the
//   entire routing engine.  Every other file includes this one.  Think of it
//   as a shared dictionary every team member carries: if the word "NetDefinition"
//   appears anywhere in the codebase, it has EXACTLY the definition written here.
//
// ─── MODERN C++ FEATURE: #pragma once ────────────────────────────────────────
// WHY: Prevents this header from being compiled twice if two different .cpp
// files both include it (directly or through other headers).
// ANALOGY: Like a "do not make a second photocopy" stamp on a document.
// The compiler sees this stamp and skips re-processing it.
// ALTERNATIVE: Old C used #ifndef /#define /#endif guards — more verbose and
// error-prone.  #pragma once is the modern, universally supported replacement.
// ─────────────────────────────────────────────────────────────────────────────

#include <string>
#include <vector>
#include <variant>   // C++17 — see RoutingContext below for full explanation

namespace routing_genetic_astar {

// ═══════════════════════════════════════════════════════════════════════════════
// STRUCT: GridPoint
// PURPOSE: Identifies one intersection on the 3D routing grid.
//
// Imagine a skyscraper where every floor has a grid of rooms.
//   x = column number (East-West)
//   y = row number    (North-South)
//   z = floor number  (which metal routing layer — think of it as the building
//       floor, with Metal 1 on the ground floor and Metal 6 near the roof)
//
// ─── C++ FEATURE: Default member initializers ─────────────────────────────────
// WHY: `int x{0}` means "if you create a GridPoint without specifying x, x
// starts at 0".  Prevents uninitialized-memory bugs silently corrupting data.
// ANALOGY: Like a bank form with pre-printed zeroes in every box.  You can
// override any field, but at least the blank fields aren't random garbage.
// Pattern from patterns_and_conventions.md: "Default member initializers
//   (int x{0}, not int x = 0)" — using brace-init is safer than equals-init
//   because it does not allow implicit narrowing (e.g., float→int truncation).
// ─────────────────────────────────────────────────────────────────────────────
struct GridPoint {
    int x{0};   // Column (East-West direction on chip)
    int y{0};   // Row    (North-South direction on chip)
    int z{0};   // Layer  (metal layer index, 0 = Metal 1 closest to silicon)
};

// ═══════════════════════════════════════════════════════════════════════════════
// STRUCT: BoundingBox
// PURPOSE: A 3D rectangular region of the chip, used by:
//   - GlobalPlanner GA: each net is assigned one corridor BoundingBox
//   - DetailedGridRouter: A* search is clamped inside this box
//   - CorridorRefinement: verifies via sites exist inside the box
//   - ConvergenceMonitor: oscillating subregion is described by a BoundingBox
// ═══════════════════════════════════════════════════════════════════════════════
struct BoundingBox {
    int x_min{0}, y_min{0}, layer_min{0};  // Southwest-bottom corner
    int x_max{0}, y_max{0}, layer_max{0};  // Northeast-top corner

    // ─── FUNCTION: contains ──────────────────────────────────────────────────
    // WHAT IT DOES: Checks if a given (x,y,layer) point falls inside this box.
    //
    // ANALOGY: Like checking whether a GPS coordinate falls inside a city's
    // bounding rectangle on a map.  Used by DetailedGridRouter to clamp A*
    // expansion to the corridor the GA assigned — preventing the router from
    // wandering through unrelated parts of the chip.
    //
    // ─── C++ FEATURE: [[nodiscard]] ───────────────────────────────────────────
    // WHY: The [[nodiscard]] attribute means "do not call this function and
    // ignore the result — the compiler will warn you if you do".
    //
    // ANALOGY: Imagine you get certified mail ("Nodiscard Mail") that requires
    // you to sign and open it.  You CANNOT legally ignore it.  [[nodiscard]]
    // is the compiler's equivalent: if you call contains() but do not use
    // the returned bool, the compiler prints a warning:
    //   "warning: ignoring return value of 'contains', declared with attribute
    //    nodiscard"
    // This catches bugs like:
    //   bbox.contains(x, y, z);  // oops — forgot to check the bool!
    //   if (bbox.contains(x, y, z)) { ... }  // correct usage
    //
    // ─── C++ FEATURE: const noexcept ──────────────────────────────────────────
    // `const`   — this function does not modify the BoundingBox it's called on.
    //             ANALOGY: A librarian who reads a book without changing it.
    // `noexcept` — this function will NEVER throw a C++ exception.
    //             ANALOGY: A guarantee on a restaurant menu saying "this dish
    //             never burns your mouth".  The compiler can generate faster
    //             code when it knows no exception can escape.
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] bool contains(int x, int y, int layer) const noexcept {
        return x >= x_min && x <= x_max &&
               y >= y_min && y <= y_max &&
               layer >= layer_min && layer <= layer_max;
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// STRUCT: NetDefinition
// PURPOSE: Describes ONE electrical net (a named wire that connects two or more
//          pins on the chip).
//
// EXAMPLE: The power rail VDD connects hundreds of cells — it is ONE net with
// hundreds of pins.  A single-bit data wire between a flip-flop and a buffer
// is ONE net with two pins.
// ═══════════════════════════════════════════════════════════════════════════════
struct NetDefinition {
    std::string            name;       // Net name from DEF/LEF file, e.g. "clk", "VDD"
    int                    id{-1};     // Unique integer ID (used as graph edge owner key)
    std::vector<GridPoint> pins;       // Every pin location this net must connect
};

// ═══════════════════════════════════════════════════════════════════════════════
// SECTION: Routing Context Classification Tags
//
// HOW THE ENGINE DECIDES WHAT ALGORITHM TO USE PER NET:
//
// Not all nets should be routed the same way.  A clock signal that fans out to
// 1024 flip-flops needs a carefully balanced H-tree so that clock arrives at
// every flip-flop at EXACTLY the same time.  A memory bitline needs a perfectly
// straight parallel track.  A random logic wire just needs the shortest path.
//
// The DesignAnalyzer examines the netlist and assigns ONE of these four context
// tags to the entire design.  StrategyComposer then uses it to pick the router.
//
// ─── C++ FEATURE: std::variant ────────────────────────────────────────────────
// std::variant<A, B, C> is a type-safe union — it holds exactly ONE of its
// listed types at any given time, and the compiler enforces that you handle
// every possible case.
//
// ANALOGY: A Swiss Army knife clip that can hold EXACTLY ONE tool at a time
// (blade OR corkscrew OR screwdriver).  You cannot accidentally use it as both.
// The knife KNOWS which tool is currently clipped in, and when you try to use
// it, you must check which tool is present.
//
// COMPARE TO void*: Old C code used void* pointers to pass "any" type.  That
// was like a mystery box — you had no idea what was inside, and if you guessed
// wrong, you'd get a crash with no error message.  std::variant crashes EARLIER
// (at compile time) if you forget to handle a case.
//
// HOW std::visit WORKS (used in StrategyComposer):
//   std::visit(my_lambda, my_variant);
//   → The compiler automatically calls my_lambda with the ACTUAL stored type.
//   → If my_variant holds a ClockNetworkTag, the lambda receives ClockNetworkTag.
//   → You write `if constexpr (std::is_same_v<T, ClockNetworkTag>)` to branch.
//   → The compiler GUARANTEES you handled every possible type (compiler error
//     if you miss one).
// ─────────────────────────────────────────────────────────────────────────────

// ── Tag structs (empty structs used purely as type labels) ────────────────────
// WHY EMPTY STRUCTS? The variant does not need to carry data, just the TAG
// (the identity of which kind of context we're in).  Think of them as sticky
// notes, each a different color, placed on a file folder.

struct RandomLogicTag  {};  // Standard random logic block → PathFinder negotiation
struct MemoryArrayTag  {};  // SRAM bitcell array         → Deterministic GridFill
struct ClockNetworkTag {};  // High-fanout clock net      → H-tree SpineFishboneRouter
struct MixedSignalTag  {};  // Dense mixed-signal block   → Hybrid strategy

// The RoutingContext is exactly one of the four tags above:
using RoutingContext = std::variant<
    RandomLogicTag,   // Most common
    MemoryArrayTag,   // Memory compilers, caches
    ClockNetworkTag,  // PLLs, clock trees
    MixedSignalTag    // ADC/DAC, RF blocks
>;

// ═══════════════════════════════════════════════════════════════════════════════
// STRUCT: DesignSummary
// PURPOSE: Everything DesignAnalyzer learns about the netlist in one structure.
//          Passed to GlobalPlanner, StrategyComposer, and PinAccessOracle.
// ═══════════════════════════════════════════════════════════════════════════════
struct DesignSummary {
    int             total_nets{0};          // How many distinct wires exist
    int             total_pins{0};          // Sum of all pin counts across all nets
    float           avg_fanout{0.0f};       // Average number of pins per net
    float           max_fanout{0.0f};       // Largest net (e.g. VDD might be 5000)
    float           density{0.0f};          // Pins per chip grid cell (crowding metric)
    RoutingContext  context{RandomLogicTag{}}; // Classification result
    std::vector<NetDefinition> nets;        // The full netlist
};

// ═══════════════════════════════════════════════════════════════════════════════
// STRUCTS: AlgorithmProfile, StageDescription
// PURPOSE: Self-description used by the MCP API so the Python orchestrator
//          can query "what does this engine do?" without reading C++ source code.
// ═══════════════════════════════════════════════════════════════════════════════
struct StageDescription {
    std::string name;       // e.g. "Build weighted grid graph"
    std::string purpose;    // Plain-English description of the stage
};

struct AlgorithmProfile {
    std::string                   problem_statement;
    std::string                   reference_algorithm;
    std::vector<StageDescription> stages;
    std::vector<std::string>      planned_cpp_components;
};

// ─── RoutingError ─────────────────────────────────────────────────────────────
// Used by EcoRouter and other components that return std::expected<T, RoutingError>.
// ANALOGY: An official complaint form — contains the human-readable error message
// and an optional numeric code for programmatic handling.
struct RoutingError {
    std::string msg;    // Human-readable description of what went wrong
    int         code{0};
};

} // namespace routing_genetic_astar
