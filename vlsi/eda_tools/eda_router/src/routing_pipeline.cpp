#include "routing_genetic_astar/routing_pipeline.hpp"

namespace routing_genetic_astar {

AlgorithmProfile RoutingPipeline::describe_reference_algorithm() const {
    AlgorithmProfile profile;
    profile.problem_statement =
        "Portable multi-net, multi-layer routing on a weighted 3D grid with congestion-aware costs.";
    profile.reference_algorithm =
        "Hybrid evolutionary router: Dijkstra precomputation + genetic optimization of pin order and net order + A* growth toward a routed tree.";

    profile.stages = {
        {"Build weighted grid graph", "Represent legal tracks and vias as weighted graph edges."},
        {"Precompute pin costs", "Use Dijkstra to estimate pairwise travel cost between pins."},
        {"Optimize pin order", "Use a genetic algorithm to find a good visit sequence per net."},
        {"Optimize net order", "Use a second genetic algorithm to decide which net gets routed first."},
        {"Route with A*", "Grow each net toward the already-routed tree using a KD-tree-guided heuristic."},
        {"Apply congestion penalties", "Mark used or risky areas so later searches avoid shorts and crowding."},
        {"Validate results", "Check for shorts, opens, and total route cost before accepting a solution."},
    };

    profile.planned_cpp_components = {
        "RoutingGridGraph",
        "PinOrderOptimizer",
        "NetOrderOptimizer",
        "AStarTreeRouter",
        "CongestionTracker",
        "RouteEvaluator",
        "PythonBindingsFacade",
    };

    return profile;
}

}  // namespace routing_genetic_astar
