#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// DEF subset loader — physical netlist → DesignSummary + die bounds (DBU)
//
// [City Parcel Map Analogy]: A DEF file is the county recorder's ledger: it lists
// every building lot (COMPONENT), every utility trench that must stay connected
// (NET), and the legal city limits (DIEAREA). We transcribe that ledger into our
// in-memory atlas (DesignSummary) without shipping the parchment to Python.
// ═══════════════════════════════════════════════════════════════════════════════

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "routing_genetic_astar/types.hpp"

namespace routing_genetic_astar {

struct CellPlacement {
    std::string inst_name;  // e.g. "U123"
    std::string ref_name;   // e.g. "NAND2"
    std::int64_t x_dbu{0};
    std::int64_t y_dbu{0};
    int grid_x{0};  // filled after lattice is known
    int grid_y{0};
};

struct ParsedDefDesign {
    DesignSummary summary{};
    std::vector<CellPlacement> cells{};
    std::int64_t die_xlo{0};
    std::int64_t die_ylo{0};
    std::int64_t die_xhi{0};
    std::int64_t die_yhi{0};
    int dbu_per_micron{1000};
    std::string design_name{};
};

struct DefParseError {
    std::string message;
};

// Parse a DEF file (5.x-style subset). Returns ParsedDefDesign on success.
[[nodiscard]] std::expected<ParsedDefDesign, DefParseError> parse_def_file(
    const std::string& path);

// Map every pin in summary.nets and every cell to integer grid coordinates
// inside [0, cols) × [0, rows). Uses die_* in DBU for linear scaling.
void snap_to_grid(ParsedDefDesign& design, int rows, int cols);

} // namespace routing_genetic_astar
