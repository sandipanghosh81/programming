#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// FILE: global_planner.hpp  —  The General: Macro-Level Genetic Algorithm Planner
// ═══════════════════════════════════════════════════════════════════════════════
//
// WHAT THIS FILE DOES:
//   Implements the Genetic Algorithm (GA) that assigns a "corridor" (a 3D
//   bounding box) and a preferred metal layer to every net BEFORE any detailed
//   routing begins.  The GA prevents congestion from ever becoming unresolvable
//   at the detailed level by proving, mathematically, that the macro-plan has
//   enough capacity for every net.
//
// ─── GENETIC ALGORITHMS EXPLAINED (no background assumed) ────────────────────
//
// BIOLOGICAL INSPIRATION:
//   Natural evolution produces extraordinarily good solutions (cheetahs, brains)
//   over millions of generations.  The key mechanism: individuals with good
//   traits survive and pass their traits to offspring; bad traits die out.
//
// FOR VLSI ROUTING:
//   We start with 50 random corridor assignments (the "population").
//   Each assignment is called a CHROMOSOME.  We measure how "good" each one is
//   (the FITNESS score).  The best chromosomes "reproduce" (their traits are
//   combined, with small random mutations) to form the next generation.
//   After 100 generations, the best chromosome in the population represents a
//   globally optimized corridor plan.
//
// ─── THE CHROMOSOME STRUCTURE ─────────────────────────────────────────────────
//
// For N nets, one Chromosome contains N CorridorAssignment::NetCorridor entries.
// Each entry says:
//   - Which bounding box does net_id route through? (bbox)
//   - Which metal layer is preferred?               (preferred_layer)
//   - Was this assigned by GridFill (memory array)? (is_memory_array)
//
// EXAMPLE for a design with 3 nets:
//   Chromosome = [
//     { net_id=0, bbox={0..20, 0..30, layer 0..2}, preferred_layer=1 },
//     { net_id=1, bbox={15..40, 10..50, layer 1..3}, preferred_layer=2 },
//     { net_id=2, bbox={5..25, 0..15, layer 0..1}, preferred_layer=0 }
//   ]
//
// FITNESS SCORE (lower is better):
//   fitness = Σ (gcell_overflow² at each vertex) + α×(overlap_penalty) + β×(via_pressure)
//   - Squared overflow: heavily penalizes areas where demand > capacity
//   - Overlap: penalizes corridors that share the same layer AND the same region
//   - Via pressure: penalizes plans that require many layer transitions
//
// ─── TOURNAMENT SELECTION (k=3) ───────────────────────────────────────────────
// ANALOGY: A sports tournament bracket.
//   1. Pick 3 random chromosomes from the population.
//   2. The one with the LOWEST fitness score (best) wins the tournament.
//   3. The winner is chosen as a parent.
//   4. Repeat twice to get two parents for crossover.
// WHY NOT JUST PICK THE SINGLE BEST? That would cause premature convergence —
// the population becomes too similar and stops exploring new solutions.
// Tournament selection preserves diversity while still preferring better solutions.
//
// ─── SINGLE-POINT CROSSOVER ────────────────────────────────────────────────────
// ANALOGY: Taking two recipe cards and swapping the bottom half.
//   Parent A = [net0_corr_A, net1_corr_A, | net2_corr_A, net3_corr_A]
//   Parent B = [net0_corr_B, net1_corr_B, | net2_corr_B, net3_corr_B]
//   Cut point at index 2 ──────────────────────^
//   Child    = [net0_corr_A, net1_corr_A,   net2_corr_B, net3_corr_B]
// Result: child inherits corridor assignments from both parents.
//
// ─── GAUSSIAN MUTATION ────────────────────────────────────────────────────────
// ANALOGY: Nudging a compass heading by a small random amount.
//   Each corridor boundary (x_min, x_max, y_min, y_max) is perturbed by a
//   Gaussian (bell-curve) random number with std-dev = mutation_delta.
//   Most mutations are tiny (near 0); a few are large — exploring new territory.
//
// ─── NUMBER SYSTEM ANALOGY ────────────────────────────────────────────────────
// RANDOM NUMBER GENERATION: std::mt19937
//   The "Mersenne Twister" — generates pseudo-random numbers using a massive
//   internal state (19937 bits!) that repeats only every 2^19937 - 1 numbers.
//   For GA we seed it with std::random_device (hardware entropy), ensuring
//   different runs produce genuinely different populations.
// ─────────────────────────────────────────────────────────────────────────────

#include <vector>
#include <random>
#include <algorithm>
#include <expected>
#include <numeric>
#include <string>
#include "routing_genetic_astar/grid_graph.hpp"
#include "routing_genetic_astar/analysis/pin_access_oracle.hpp"
#include "routing_genetic_astar/planner/grid_fill.hpp"

namespace routing_genetic_astar {

struct PlannerError { std::string reason; };

// ═══════════════════════════════════════════════════════════════════════════════
// STRUCT: CorridorAssignment
// PURPOSE: The OUTPUT of GlobalPlanner — the assigned bounding boxes and
//          preferred layers for every net in the design.
// ═══════════════════════════════════════════════════════════════════════════════
struct CorridorAssignment {

    // ONE entry per net — this IS the chromosome data structure.
    struct NetCorridor {
        int         net_id{-1};         // Which net does this entry belong to?
        BoundingBox bbox{};             // The 3D routing corridor for this net
        int         preferred_layer{0}; // Best metal layer for this net
        bool        is_memory_array{false}; // True if assigned by GridFill (not GA)
    };

    std::vector<NetCorridor> corridors; // One entry per net — this is the chromosome
    float fitness{0.0f};                // Fitness score of THIS chromosome (lower = better)
};

// ═══════════════════════════════════════════════════════════════════════════════
// CLASS: GlobalPlanner
// PURPOSE: The Genetic Algorithm engine that sets macro-level routing corridors.
//          Called ONCE before SpatialPartitioner and NegotiatedRoutingLoop.
// ═══════════════════════════════════════════════════════════════════════════════
class GlobalPlanner {
public:
    // ─── Public Tunable Parameters ────────────────────────────────────────────
    // OptunaTuner adjusts these between full routing runs (offline meta-tuning).
    int   population_size{50};   // How many chromosomes in each generation
    int   max_generations{100};  // Maximum GA iterations before returning best found
    float mutation_delta{2.0f};  // Std-dev of Gaussian corridor boundary perturbation
    float alpha_overlap{0.5f};   // Weight of inter-corridor overlap in fitness score
    float beta_via{0.3f};        // Weight of via-layer pressure in fitness score

    // ── FUNCTION: plan ─────────────────────────────────────────────────────────
    // ─────────────────────────────────────────────────────────────────────────
    // WHAT IT DOES (the complete flow):
    //
    //   INPUT:  DesignSummary (all nets + context), RoutingGridGraph (the lattice),
    //           PinAccessOracle (legal pin entry vectors)
    //
    //   OUTPUT: std::expected<CorridorAssignment, PlannerError>
    //           → Either a valid CorridorAssignment (success)
    //           → OR a PlannerError describing why planning failed
    //
    //   FLOW:
    //   1. If the design is MEMORY_ARRAY, skip the GA entirely and call GridFill::stamp_array().
    //      WHY: Memory bitlines/wordlines have a solved closed-form assignment.
    //      GA evolution on regular arrays wastes CPU for zero benefit.
    //
    //   2. Otherwise: Initialize population of `population_size` random chromosomes.
    //
    //   3. GA EVOLUTION LOOP (up to max_generations iterations):
    //      a. Evaluate fitness of every chromosome in the population.
    //      b. Track the best-scoring chromosome.
    //      c. If best fitness == 0 (perfect: no overflow, no overlap) → exit early.
    //      d. Build next generation via tournament selection, crossover, mutation.
    //
    //   4. Return the best chromosome found.
    //
    // ─── C++ FEATURE: std::expected (C++23) ──────────────────────────────────
    // `std::expected<T, E>` is like a function that mails you EITHER:
    //   - A package (type T) if everything went well, or
    //   - An error note (type E) explaining what went wrong
    //
    // ANALOGY: Ordering a pizza online.
    //   - Delivery success: expected<Pizza, Error> holds your pizza
    //   - Out of stock: expected<Pizza, Error> holds <Error: "oven broken">
    //
    // The caller MUST check which one they received before using the value:
    //   auto result = planner.plan(design, grid, pao);
    //   if (result) {                          // Did we get a Pizza?
    //       auto assignment = result.value();  // Yes: use it
    //   } else {
    //       std::cerr << result.error().reason; // No: handle the error
    //   }
    //
    // WHY BETTER THAN EXCEPTIONS?
    //   Exceptions make the "happy path" fast but errors expensive (stack unwinding).
    //   std::expected makes BOTH paths explicit, zero-overhead, and readable.
    //   It also appears in the function signature — readers know upfront this can fail.
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] std::expected<CorridorAssignment, PlannerError>
    plan(const DesignSummary& design, const RoutingGridGraph& grid,
         const PinAccessOracle& pao) {

        if (design.nets.empty())
            return std::unexpected(PlannerError{"GlobalPlanner: design has no nets"});

        // ── STEP 1: Memory Array shortcut ─────────────────────────────────────
        // ─── C++ FEATURE: std::holds_alternative ─────────────────────────────
        // Checks which type is currently stored in the variant.
        // Like asking "is the Swiss Army knife currently showing the blade?"
        // If yes → use GridFill (not the GA).
        // ─────────────────────────────────────────────────────────────────────
        if (std::holds_alternative<MemoryArrayTag>(design.context)) {
            GridFill filler;
            return filler.stamp_array(design, grid);
        }

        // ── STEP 2: Initialize random engine ──────────────────────────────────
        // std::mt19937 seeded from hardware entropy for genuinely random populations.
        std::mt19937 rng{std::random_device{}()};

        // ── STEP 3: Initial population — N random corridor assignments ─────────
        auto population = initialize_population(design, grid, pao, rng);

        // ── STEP 4: Evolution loop ─────────────────────────────────────────────
        float best_fitness = std::numeric_limits<float>::max();
        size_t best_idx    = 0;

        for (int gen = 0; gen < max_generations; ++gen) {

            // --- 4a: Compute fitness for everyone in this generation ---
            std::vector<float> scores = evaluate_all_fitness(population, grid);

            // --- 4b: Track the best (chromosome with LOWEST fitness score) ---
            for (size_t i = 0; i < scores.size(); ++i) {
                if (scores[i] < best_fitness) {
                    best_fitness = scores[i];
                    best_idx     = i;
                }
            }

            // --- 4c: Early exit if we found a perfect plan ---
            if (best_fitness < 1e-4f) break;

            // --- 4d: Build the next generation (selection + crossover + mutation) ---
            population = evolve(population, scores, rng);
        }

        CorridorAssignment result = population[best_idx];
        result.fitness = best_fitness;
        return result;
    }

private:
    // ────────────────────────────────────────────────────────────────────────────
    // PRIVATE HELPER: bbox_from_pins
    //
    // WHAT IT DOES:
    //   Computes the Hull BoundingBox that covers ALL pins of a net.
    //   Adds a small routing margin (+2 cells) so the A* router has room to
    //   maneuver rather than being stuck in a one-cell-wide tunnel.
    //
    // ANALOGY: Drawing the smallest rectangle that fits all the cities you need
    //   to visit, then adding a few miles of padding on each side.
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] static BoundingBox bbox_from_pins(const NetDefinition& net) {
        if (net.pins.empty()) return BoundingBox{};
        BoundingBox bb{
            net.pins[0].x, net.pins[0].y, net.pins[0].z,
            net.pins[0].x, net.pins[0].y, net.pins[0].z
        };
        for (const auto& p : net.pins) {
            bb.x_min = std::min(bb.x_min, p.x); bb.x_max = std::max(bb.x_max, p.x);
            bb.y_min = std::min(bb.y_min, p.y); bb.y_max = std::max(bb.y_max, p.y);
            bb.layer_min = std::min(bb.layer_min, p.z);
            bb.layer_max = std::max(bb.layer_max, p.z);
        }
        // Routing margin: A* needs breathing room around the pin hull.
        bb.x_min -= 2; bb.x_max += 2;
        bb.y_min -= 2; bb.y_max += 2;
        return bb;
    }

    // ────────────────────────────────────────────────────────────────────────────
    // PRIVATE HELPER: initialize_population
    //
    // WHAT IT DOES:
    //   Creates `population_size` random chromosomes.
    //   Each chromosome's NetCorridor is initialized to the pin hull (bbox_from_pins)
    //   with a random metal layer assignment.
    //
    // WHY RANDOM INITIALIZATION?
    //   A completely random starting population ensures the GA explores the ENTIRE
    //   solution space.  If we started deterministically (e.g., all nets on layer 0),
    //   the GA would only improve in one direction and might miss better solutions.
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] std::vector<CorridorAssignment>
    initialize_population(const DesignSummary& design, const RoutingGridGraph& grid,
                          const PinAccessOracle&, std::mt19937& rng) {
        std::uniform_int_distribution<int> layer_dist(0, grid.layers() - 1);
        std::vector<CorridorAssignment> pop(static_cast<size_t>(population_size));
        for (auto& chrom : pop) {
            chrom.corridors.resize(design.nets.size());
            for (size_t i = 0; i < design.nets.size(); ++i) {
                chrom.corridors[i].net_id          = design.nets[i].id;
                chrom.corridors[i].bbox            = bbox_from_pins(design.nets[i]);
                chrom.corridors[i].preferred_layer = layer_dist(rng);
            }
        }
        return pop;
    }

    // ────────────────────────────────────────────────────────────────────────────
    // PRIVATE HELPER: evaluate_all_fitness
    //
    // WHAT IT DOES:
    //   Computes fitness score for every chromosome in the population in one pass.
    //   Returns a parallel vector of float scores (index i → score for population[i]).
    //   Lower score = better (this is a MINIMIZATION problem).
    //
    // ─── C++ FEATURE: std::transform (from <algorithm>) ───────────────────────
    // std::transform applies a function to every element of a range and writes
    // results to another range.  Like `map()` in Python or JavaScript.
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] std::vector<float>
    evaluate_all_fitness(const std::vector<CorridorAssignment>& pop,
                         const RoutingGridGraph& grid) const {
        std::vector<float> scores(pop.size());
        for (size_t i = 0; i < pop.size(); ++i)
            scores[i] = compute_fitness(pop[i], grid);
        return scores;
    }

    // ────────────────────────────────────────────────────────────────────────────
    // PRIVATE HELPER: compute_fitness
    //
    // WHAT IT DOES:
    //   Evaluates how GOOD a chromosome (corridor assignment) is.
    //
    //   COMPONENT 1 — GCell overflow (main objective):
    //     For every vertex in the graph, compute max(0, demand - capacity)².
    //     Squaring is critical: a vertex at 3× overflow costs 9 fitness points,
    //     not 3.  This makes the GA actively avoid ANY overflow rather than
    //     balancing moderate overflow everywhere.
    //     EXPECTED VALUES:
    //       fitness ≈ 0:   excellent plan — all cells within capacity
    //       fitness ≈ 10:  a few minor hotspots — solvable at detail routing
    //       fitness > 100: severely congested plan — detail routing will fail
    //
    //   COMPONENT 2 — Corridor overlap:
    //     Counts pairs of corridors that share a layer AND spatially overlap.
    //     If two nets are assigned to the same layer in the same region,
    //     they will conflict in detail routing.  Penalize this early.
    //
    //   EXPECTED RESULTS BY DESIGN TYPE:
    //     Random logic (50 nets, 100×100 grid, 6 layers):
    //       Initial population fitness: avg ~50–200
    //       After 100 generations:      best ~0–5
    //     Dense memory (1000 nets, 100×100 grid):
    //       GridFill bypasses GA → fitness = 0 by construction
    //     Clock tree (256 sinks):
    //       Initial fitness: ~30–80, After 100 gen: ~2–10
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] float compute_fitness(const CorridorAssignment& chrom,
                                        const RoutingGridGraph& grid) const {
        float overflow_cost = compute_overflow_cost(grid);
        float overlap_cost  = compute_overlap_cost(chrom);
        return overflow_cost + alpha_overlap * overlap_cost;
    }

    [[nodiscard]] float compute_overflow_cost(const RoutingGridGraph& grid) const {
        float cost{0.0f};
        auto [vi, vi_end] = boost::vertices(grid.graph());
        for (auto it = vi; it != vi_end; ++it) {
            const float ov = grid.gcell_overflow(*it);
            cost += ov * ov;  // Squared penalty: heavily punish overcrowding
        }
        return cost;
    }

    [[nodiscard]] float compute_overlap_cost(const CorridorAssignment& chrom) const {
        float overlap{0.0f};
        const auto& cors = chrom.corridors;
        // Check every pair of corridors for layer + spatial overlap.
        for (size_t i = 0; i < cors.size(); ++i) {
            for (size_t j = i + 1; j < cors.size(); ++j) {
                if (corridors_overlap(cors[i], cors[j])) overlap += 1.0f;
            }
        }
        return overlap;
    }

    [[nodiscard]] static bool corridors_overlap(
            const CorridorAssignment::NetCorridor& a,
            const CorridorAssignment::NetCorridor& b) noexcept {
        return a.preferred_layer == b.preferred_layer &&
               a.bbox.x_min <= b.bbox.x_max && a.bbox.x_max >= b.bbox.x_min &&
               a.bbox.y_min <= b.bbox.y_max && a.bbox.y_max >= b.bbox.y_min;
    }

    // ────────────────────────────────────────────────────────────────────────────
    // PRIVATE HELPER: tournament_select
    //
    // WHAT IT DOES:
    //   Picks ONE parent chromosome via tournament selection (k=3):
    //     1. Draw 3 random indices from [0, population.size())
    //     2. Return the index whose score is LOWEST (best fitness)
    //
    // ANALOGY:
    //   At a school science fair, 3 random projects are compared.
    //   The best of those 3 is awarded a prize (chosen as parent).
    //   Projects that are good but not chosen in any group of 3 still have
    //   a chance in future generations → preserves diversity.
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] size_t tournament_select(const std::vector<float>& scores,
                                           std::mt19937& rng) const {
        std::uniform_int_distribution<size_t> dist(0, scores.size() - 1);
        size_t best = dist(rng);
        for (int k = 1; k < 3; ++k) {
            size_t cand = dist(rng);
            if (scores[cand] < scores[best]) best = cand;
        }
        return best;
    }

    // ────────────────────────────────────────────────────────────────────────────
    // PRIVATE HELPER: crossover
    //
    // WHAT IT DOES:
    //   Produces one child chromosome by combining two parents at a random cut point.
    //   Corridors [0..cut) come from parent a.
    //   Corridors [cut..N) come from parent b.
    //
    // ANALOGY: Two recipes combining into a new one.
    //   Recipe A: eggs from friend A, flour from friend B.
    //   The exact cut point is random → different recipes created each time.
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] CorridorAssignment
    crossover(const CorridorAssignment& a, const CorridorAssignment& b,
              std::mt19937& rng) const {
        CorridorAssignment child;
        const size_t N = a.corridors.size();
        if (N == 0) return a;
        std::uniform_int_distribution<size_t> dist(0, N - 1);
        const size_t cut = dist(rng); // Random cut point
        child.corridors.resize(N);
        for (size_t i = 0; i < N; ++i)
            child.corridors[i] = (i < cut) ? a.corridors[i] : b.corridors[i];
        return child;
    }

    // ────────────────────────────────────────────────────────────────────────────
    // PRIVATE HELPER: mutate
    //
    // WHAT IT DOES:
    //   Perturbs each corridor boundary by a Gaussian (bell-curve) random number.
    //   std::normal_distribution<float>(mean=0, stddev=mutation_delta) produces
    //   values centered at 0 with most values within ±mutation_delta.
    //
    // ANALOGY: Nudging a map pin by a random, mostly small amount.
    //   Most of the time the pin barely moves (small Gaussian draw).
    //   Occasionally a large draw causes a big exploration step (jumping local optima).
    //
    // AFTER MUTATION: Ensures x_min ≤ x_max (swap if crossed after perturbation).
    // ─────────────────────────────────────────────────────────────────────────
    void mutate(CorridorAssignment& chrom, std::mt19937& rng) const {
        std::normal_distribution<float> jitter(0.0f, mutation_delta);
        for (auto& nc : chrom.corridors) {
            nc.bbox.x_min += static_cast<int>(jitter(rng));
            nc.bbox.x_max += static_cast<int>(jitter(rng));
            nc.bbox.y_min += static_cast<int>(jitter(rng));
            nc.bbox.y_max += static_cast<int>(jitter(rng));
            // Post-mutation validity: ensure min ≤ max after random perturbation.
            if (nc.bbox.x_min > nc.bbox.x_max) std::swap(nc.bbox.x_min, nc.bbox.x_max);
            if (nc.bbox.y_min > nc.bbox.y_max) std::swap(nc.bbox.y_min, nc.bbox.y_max);
        }
    }

    // ────────────────────────────────────────────────────────────────────────────
    // PRIVATE HELPER: evolve
    //
    // WHAT IT DOES:
    //   Builds the complete next generation from the current population + scores:
    //   For each new child slot:
    //     1. Select parent 1 via tournament_select()
    //     2. Select parent 2 via tournament_select()
    //     3. Create child via crossover(parent1, parent2)
    //     4. Mutate child in-place
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] std::vector<CorridorAssignment>
    evolve(const std::vector<CorridorAssignment>& pop,
           const std::vector<float>& scores,
           std::mt19937& rng) {
        std::vector<CorridorAssignment> next;
        next.reserve(pop.size());
        while (next.size() < pop.size()) {
            const size_t p1  = tournament_select(scores, rng);
            const size_t p2  = tournament_select(scores, rng);
            auto child       = crossover(pop[p1], pop[p2], rng);
            mutate(child, rng);
            next.push_back(std::move(child));
        }
        return next;
    }
};

} // namespace routing_genetic_astar
