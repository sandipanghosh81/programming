#include <iostream>

#include "routing_genetic_astar/routing_pipeline.hpp"

int main() {
    routing_genetic_astar::RoutingPipeline pipeline;
    const auto profile = pipeline.describe_reference_algorithm();

    std::cout << "Routing project scaffold ready.\n";
    std::cout << "Problem: " << profile.problem_statement << '\n';
    std::cout << "Reference algorithm: " << profile.reference_algorithm << "\n\n";

    std::cout << "Pipeline stages:\n";
    for (const auto& stage : profile.stages) {
        std::cout << " - " << stage.name << ": " << stage.purpose << '\n';
    }

    std::cout << "\nPlanned C++ components:\n";
    for (const auto& component : profile.planned_cpp_components) {
        std::cout << " - " << component << '\n';
    }

    return 0;
}
