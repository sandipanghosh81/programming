#pragma once
#include "routing_genetic_astar/planner/global_planner.hpp"
#include "routing_genetic_astar/grid_graph.hpp"

namespace routing_genetic_astar {

// ═══════════════════════════════════════════════════════════════════════════════
// GridFill implementation — included via separate translation unit to break
// the circular dependency with global_planner.hpp.
// ═══════════════════════════════════════════════════════════════════════════════
inline std::expected<CorridorAssignment, PlannerError>
GridFill::stamp_array(const DesignSummary& design, const RoutingGridGraph& grid) {
    if (design.nets.empty())
        return std::unexpected(PlannerError{"GridFill: no nets"});

    CorridorAssignment result;
    result.corridors.resize(design.nets.size());
    result.fitness = 0.0f;

    // Assign bitlines (nets with even index) to even metal layers (preferred H),
    // wordlines (nets with odd index) to odd metal layers (preferred V).
    // Each net gets a 1-track-wide corridor spanning the memory array extent.
    const int grid_rows = grid.rows();
    const int grid_cols = grid.cols();

    for (size_t i = 0; i < design.nets.size(); ++i) {
        auto& nc           = result.corridors[i];
        nc.net_id          = design.nets[i].id;
        nc.is_memory_array = true;

        if (i % 2 == 0) {
            // Bitline: vertical track, even layer, column index = i/2
            const int col          = static_cast<int>(i / 2) % grid_cols;
            nc.preferred_layer     = 0;       // Even layer → horizontal metal preferred as bitline
            nc.bbox.x_min          = col;
            nc.bbox.x_max          = col;
            nc.bbox.y_min          = 0;
            nc.bbox.y_max          = grid_rows - 1;
            nc.bbox.layer_min      = 0;
            nc.bbox.layer_max      = 0;
        } else {
            // Wordline: horizontal track, odd layer, row index = (i-1)/2
            const int row          = static_cast<int>((i - 1) / 2) % grid_rows;
            nc.preferred_layer     = 1;       // Odd layer → vertical metal preferred as wordline
            nc.bbox.x_min          = 0;
            nc.bbox.x_max          = grid_cols - 1;
            nc.bbox.y_min          = row;
            nc.bbox.y_max          = row;
            nc.bbox.layer_min      = 1;
            nc.bbox.layer_max      = 1;
        }
    }

    return result;
}

} // namespace routing_genetic_astar
