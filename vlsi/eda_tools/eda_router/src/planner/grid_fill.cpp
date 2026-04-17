#include "routing_genetic_astar/planner/grid_fill.hpp"
#include "routing_genetic_astar/planner/global_planner.hpp"
#include <algorithm>

namespace routing_genetic_astar {

std::expected<CorridorAssignment, PlannerError>
GridFill::stamp_array(const DesignSummary& design, const RoutingGridGraph& /* grid */) {
    CorridorAssignment assignment;
    assignment.corridors.reserve(design.nets.size());
    for (const auto& net : design.nets) {
        CorridorAssignment::NetCorridor nc;
        nc.net_id = net.id;
        nc.preferred_layer = (net.id % 2 == 0) ? 0 : 1;
        nc.is_memory_array = true;
        if (!net.pins.empty()) {
            nc.bbox.x_min = net.pins[0].x; nc.bbox.x_max = net.pins[0].x;
            nc.bbox.y_min = net.pins[0].y; nc.bbox.y_max = net.pins[0].y;
            for (const auto& p : net.pins) {
                nc.bbox.x_min = std::min(nc.bbox.x_min, p.x);
                nc.bbox.x_max = std::max(nc.bbox.x_max, p.x);
                nc.bbox.y_min = std::min(nc.bbox.y_min, p.y);
                nc.bbox.y_max = std::max(nc.bbox.y_max, p.y);
            }
            nc.bbox.x_min -= 1; nc.bbox.x_max += 1;
            nc.bbox.y_min -= 1; nc.bbox.y_max += 1;
        }
        assignment.corridors.push_back(nc);
    }
    assignment.fitness = 0.0f;
    return assignment;
}

} // namespace routing_genetic_astar
