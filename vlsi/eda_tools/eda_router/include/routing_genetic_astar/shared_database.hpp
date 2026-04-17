#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "routing_genetic_astar/grid_graph.hpp"
#include "routing_genetic_astar/io/def_design_loader.hpp"
#include "routing_genetic_astar/types.hpp"

// ═══════════════════════════════════════════════════════════════════════════════
// SharedDatabase — single in-process design state for all MCP servers
// ═══════════════════════════════════════════════════════════════════════════════
struct SharedDatabase {
    bool is_loaded = false;
    std::string design_name = "None";
    int num_nets = 0;
    int num_cells = 0;

    routing_genetic_astar::RoutingGridGraph routing_grid;

    // Populated by DEF load (or synthetic demo): full netlist + placements stay in C++.
    routing_genetic_astar::DesignSummary design_summary{};
    std::vector<routing_genetic_astar::CellPlacement> cell_placements{};
    std::int64_t die_xlo{0};
    std::int64_t die_ylo{0};
    std::int64_t die_xhi{0};
    std::int64_t die_yhi{0};
    int dbu_per_micron{1000};
};
