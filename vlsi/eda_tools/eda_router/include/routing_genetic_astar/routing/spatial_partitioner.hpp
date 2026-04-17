#pragma once
#include <thread>
#include <functional>
#include <barrier>
#include <vector>
#include "routing_genetic_astar/planner/global_planner.hpp"
#include "routing_genetic_astar/grid_graph.hpp"

namespace routing_genetic_astar {

// ── Region descriptor assigned to one jthread worker ──────────────────────────
struct PartitionRegion {
    BoundingBox bbox;           // Thread-exclusive geographic area
    std::vector<int> net_ids;   // Nets whose corridors fall inside this region
    int region_id{-1};
};

// ═══════════════════════════════════════════════════════════════════════════════
// SpatialPartitioner — Section 4.3 and Section 5 of architecture_v3.md
//
// Takes track-feasible corridor assignments from CorridorRefinement and carves
// the chip into N thread-safe geographic regions (horizontal strips).
// Each region is dispatched to a C++20 std::jthread worker.
// Boundary cells are marked as ghost cells in RoutingGridGraph (read-only for
// non-owner threads); CrossRegionMediator handles nets that span boundaries.
//
// [The School Field Trip Analogy]: std::barrier is the teacher at the museum exit.
// All N threads independently explore their region (route their nets), then arrive
// at the barrier.  The bus (CrossRegionMediator) cannot leave until the LAST thread
// has arrived at the barrier exit checkpoint.
// ═══════════════════════════════════════════════════════════════════════════════
class SpatialPartitioner {
public:
    // Partition the chip into horizontal strips, one per thread.
    // Returns the list of region descriptors (one per thread).
    [[nodiscard]] std::vector<PartitionRegion>
    partition(const CorridorAssignment& assignment,
              const RoutingGridGraph& grid,
              int num_threads) const {
        const int strip_height =
            std::max(1, (grid.rows() + num_threads - 1) / num_threads);

        std::vector<PartitionRegion> regions;
        regions.reserve(static_cast<size_t>(num_threads));

        for (int t = 0; t < num_threads; ++t) {
            PartitionRegion reg;
            reg.region_id    = t;
            reg.bbox.x_min   = 0;
            reg.bbox.x_max   = grid.cols() - 1;
            reg.bbox.y_min   = t * strip_height;
            reg.bbox.y_max   = std::min((t + 1) * strip_height - 1, grid.rows() - 1);
            reg.bbox.layer_min = 0;
            reg.bbox.layer_max = grid.layers() - 1;

            // Assign nets whose corridors intersect with this strip.
            for (const auto& nc : assignment.corridors) {
                if (nc.bbox.y_min <= reg.bbox.y_max && nc.bbox.y_max >= reg.bbox.y_min)
                    reg.net_ids.push_back(nc.net_id);
            }
            regions.push_back(std::move(reg));
        }

        // Mark ghost cells on region boundaries inside the RoutingGridGraph.
        // Ghost cells are the bottom row of the region above and the top row of the
        // region below — readable by both adjacent threads, writable only by
        // CrossRegionMediator after barrier sync.
        // Note: const_cast needed because this is a topology operation done once at setup.
        auto& mutable_grid = const_cast<RoutingGridGraph&>(grid);
        for (int t = 1; t < num_threads; ++t) {
            const int boundary_y = t * strip_height - 1;
            if (boundary_y < 0 || boundary_y >= grid.rows()) continue;
            for (int x = 0; x < grid.cols(); ++x) {
                for (int l = 0; l < grid.layers(); ++l) {
                    if (grid.in_bounds(x, boundary_y, l))
                        mutable_grid.mark_ghost(mutable_grid.vertex_at(x, boundary_y, l));
                }
            }
        }

        return regions;
    }

    // Dispatch N jthreads, one per region.  The caller provides the worker lambda.
    // [std::jthread Analogy]: Unlike std::thread, jthread automatically joins on
    // destruction — no risk of std::terminate if the thread object goes out of scope.
    void dispatch(const std::vector<PartitionRegion>& regions,
                  std::function<void(const PartitionRegion&)> worker,
                  std::barrier<>& sync_point) {
        std::vector<std::jthread> pool;
        pool.reserve(regions.size());
        for (const auto& reg : regions) {
            // Capture reg BY VALUE: the loop variable's address must not dangle across jthreads.
            pool.emplace_back([reg, &worker, &sync_point]() {
                worker(reg);
                sync_point.arrive_and_wait();
            });
        }
        // Threads join automatically when pool goes out of scope (jthread RAII).
    }
};

} // namespace routing_genetic_astar
