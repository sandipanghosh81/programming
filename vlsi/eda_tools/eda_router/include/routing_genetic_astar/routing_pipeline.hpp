#pragma once

#include "routing_genetic_astar/types.hpp"

namespace routing_genetic_astar {

class RoutingPipeline {
public:
    // [Unopened Mail Analogy]: [[nodiscard]] prevents orchestrators from discarding the heavy descriptive payload.
    [[nodiscard]] AlgorithmProfile describe_reference_algorithm() const;
};

}  // namespace routing_genetic_astar
