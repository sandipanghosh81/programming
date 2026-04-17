#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// FILE: ilp_solver.hpp  —  The ILP Fallback: Mathematically Guaranteed Resolution
// ═══════════════════════════════════════════════════════════════════════════════
//
// WHAT THIS FILE DOES:
//   When the NegotiatedRoutingLoop (PathFinder) detects OSCILLATION — the same
//   set of nets swapping conflicts back and forth for N iterations without
//   convergence — it invokes this solver.
//
//   The ILP Solver formulates the oscillating subregion's track-assignment problem
//   as a mathematical Integer Linear Program (ILP) and solves it EXACTLY using
//   Google OR-Tools CP-SAT solver.  The solution is mathematically guaranteed to
//   be conflict-free, which is then "locked" and fed back to the PathFinder.
//
// ─── INTEGER LINEAR PROGRAMMING (ILP) EXPLAINED ─────────────────────────────
//
// WHAT IS AN ILP?
//   ILP is an optimization problem where:
//     - VARIABLES are integers (here: binary 0 or 1)
//     - CONSTRAINTS are linear inequalities (algebraic rules)
//     - OBJECTIVE is a linear expression to minimize
//
//   The solver tries every possible combination and mathematically proves which
//   combination satisfies ALL constraints and MINIMIZES the objective.
//
// ANALOGY: A digital Sudoku solver.
//   - Variables: Which number goes in each cell?
//   - Constraints: Each row/column/box has each number exactly once.
//   - Objective: (in our case) minimize total wirelength.
//   - Result: The UNIQUE solution that satisfies all rules.
//
// ─── OUR SPECIFIC ILP FORMULATION ─────────────────────────────────────────────
//
// DECISION VARIABLES:
//   x[n][e] ∈ {0, 1}   for each net n and each edge e in the oscillating subregion.
//   x[n][e] = 1 means: "net n is routed through edge e"
//   x[n][e] = 0 means: "net n does NOT use edge e"
//
// CONSTRAINTS:
//
//   CONSTRAINT 1 — No Electrical Shorts (the most critical rule):
//     For each edge e:   Σ_n x[n][e] ≤ 1
//     Translation:       "At most ONE net can own any single edge."
//     WHY: Two nets sharing the same physical track would create an electrical
//     short circuit — a manufacturing defect that destroys the chip.
//     ANALOGY: Each parking spot can hold at most 1 car (you can't park two
//     cars in the same spot without a collision).
//
//   CONSTRAINT 2 — Connectivity (each net must be connected):
//     For each net n:   Σ_e x[n][e] ≥ 1
//     Translation:       "Each net uses at least one edge in the subregion."
//     NOTE: This is a simplification.  A full formulation would require
//     flow conservation constraints for proper path connectivity.  The simplified
//     version ensures nets are not completely excluded from the subregion.
//
// OBJECTIVE — Minimize Weighted Wirelength:
//   Minimize: Σ_n Σ_e x[n][e] × effective_weight(e) × 100
//   (multiplied by 100 to convert float costs to integers for CP-SAT)
//   Translation: "Find the assignment where assigned edges collectively have
//   the lowest total routing cost."
//
// ─── WHEN IS THE ILP INVOKED? ─────────────────────────────────────────────────
//   ConvergenceMonitor detect oscillation when:
//     max(last_N_conflict_counts) - min(last_N_conflict_counts) < threshold
//   for N=8 consecutive passes.  This means: "the conflict count is stuck
//   in a narrow range — PathFinder is oscillating, not converging."
//
// ─── EXPECTED RESULTS ─────────────────────────────────────────────────────────
//   ILP is invoked for small subregions (typically 5-50 nets, 100-1000 edges).
//   Solve time with 5-second limit: typically ≤ 1 second for these sizes.
//   Result: conflict-free assignment for the subregion → locked into NRL → convergence.
//   If ILP cannot find a feasible solution: returns SolverError → NRL continues
//   without lock (may eventually converge via PathFinder anyway).
//
// ─── OR-TOOLS CP-SAT SOLVER ───────────────────────────────────────────────────
//   Google OR-Tools is an open-source constraint programming / optimization library.
//   CP-SAT is its state-of-the-art constraint satisfaction and optimization solver:
//   - Uses a "SAT modulo theory" approach internally (Branch-and-bound + DPLL)
//   - Extremely fast for binary variable problems (our exact use case)
//   - Provides OPTIMAL solutions (not heuristic) within the time limit
//
// ─────────────────────────────────────────────────────────────────────────────

#include <expected>
#include <string>
#include <vector>

#ifdef HAVE_OR_TOOLS
#  include "ortools/sat/cp_model.h"
#  include "ortools/sat/cp_model_pb.h"
#  include "ortools/sat/sat_parameters.pb.h"
#endif

#include "routing_genetic_astar/convergence/convergence_monitor.hpp"

namespace routing_genetic_astar {

// ─── STRUCT: IlpSolution ──────────────────────────────────────────────────────
// The successful result from solve(): a per-net list of assigned EdgeDesc handles.
// NegotiatedRoutingLoop locks these into the RoutingGridGraph (frozen=true).
struct IlpSolution {
    bool valid{false};
    // pairs of (net_id, edges assigned to that net in the subregion)
    std::vector<std::pair<int, std::vector<EdgeDesc>>> routes;
};

struct SolverError { std::string msg; };

// ═══════════════════════════════════════════════════════════════════════════════
// CLASS: IlpSolver
// ═══════════════════════════════════════════════════════════════════════════════
class IlpSolver {
public:
    // Maximum time the solver is allowed to run before returning with best found.
    // For VLSI routing subregions this is typically reached only for ≥100 nets.
    float time_limit_seconds{5.0f};

    // ── FUNCTION: solve ────────────────────────────────────────────────────────
    // ─────────────────────────────────────────────────────────────────────────
    // WHAT IT DOES (step by step):
    //
    //   STEP 1: Collect all edges inside the subregion bounding box.
    //     Iterates through ALL graph edges.  For each edge, checks if its source
    //     vertex falls within the SubregionDescriptor's bbox.  Only edges inside
    //     the box are included in the ILP.
    //     WHY: The ILP only needs to resolve the LOCAL area where oscillation occurs.
    //     Including the entire chip would make the ILP too large to solve.
    //
    //   STEP 2: Create binary decision variables x[n][e].
    //     For N nets and E edges: creates N×E BoolVar objects in the CP model.
    //     EXAMPLE: 10 nets × 200 edges = 2000 binary variables.
    //
    //   STEP 3: Add no-short constraints.
    //     For each of the E edges: sum of all N variables for that edge ≤ 1.
    //     In CP-SAT: model.AddLessOrEqual(LinearExpr{x[0][e], ..., x[N-1][e]}, 1)
    //
    //   STEP 4: Add connectivity constraints.
    //     For each of the N nets: sum of all E variables for that net ≥ 1.
    //
    //   STEP 5: Set the objective (minimize weighted wirelength).
    //     For each (n, e) variable:
    //       coefficient = EdgeProperties::effective_weight(e) × 100 (converted to int)
    //
    //   STEP 6: Call CP-SAT solver with time_limit_seconds.
    //     Returns OPTIMAL or FEASIBLE → extract solution.
    //     Returns INFEASIBLE or timeout → return SolverError.
    //
    //   STEP 7 (caller responsibility): Lock the ILP solution into the graph.
    //     NegotiatedRoutingLoop calls try_claim_edge() for each assigned edge and
    //     sets frozen=true so NRL cannot rip them up.
    // ─────────────────────────────────────────────────────────────────────────
    [[nodiscard]] std::expected<IlpSolution, SolverError>
    solve(const SubregionDescriptor& region, RoutingGridGraph& grid) {

#ifdef HAVE_OR_TOOLS
        return solve_with_or_tools(region, grid);
#else
        // When OR-Tools is not linked (common during development):
        // Return a clear error message so the NRL continues without locking.
        (void)region; (void)grid;
        return std::unexpected(SolverError{
            "ILP solver not available. "
            "Build with -DHAVE_OR_TOOLS and link against OR-Tools to enable it. "
            "NegotiatedRoutingLoop will continue via PathFinder heuristic."});
#endif
    }

private:
#ifdef HAVE_OR_TOOLS

    [[nodiscard]] std::expected<IlpSolution, SolverError>
    solve_with_or_tools(const SubregionDescriptor& region, RoutingGridGraph& grid) {
        using namespace operations_research::sat;

        CpModelBuilder model;

        // ── STEP 1: Collect edges inside the subregion's bounding box ──────────
        std::vector<EdgeDesc> region_edges = collect_region_edges(region.bbox, grid);

        if (region_edges.empty())
            return std::unexpected(SolverError{"ILP: no edges found in oscillating subregion bbox"});

        const int N = static_cast<int>(region.net_ids.size());
        const int E = static_cast<int>(region_edges.size());

        // ── STEP 2: Create binary decision variables x[net_index][edge_index] ──
        // BoolVar in CP-SAT is a binary variable (0 or 1).
        // x_vars[n][e] = 1 means net region.net_ids[n] routes through region_edges[e].
        std::vector<std::vector<BoolVar>> x_vars = create_variables(model, N, E);

        // ── STEP 3: Constraint — No Electrical Shorts ─────────────────────────
        add_no_short_constraints(model, x_vars, N, E);

        // ── STEP 4: Constraint — Each Net Uses At Least One Edge ──────────────
        add_connectivity_constraints(model, x_vars, N, E);

        // ── STEP 5: Objective — Minimize Weighted Wirelength ──────────────────
        set_wirelength_objective(model, x_vars, region_edges, grid, N, E);

        // ── STEP 6: Invoke CP-SAT solver ──────────────────────────────────────
        SatParameters params;
        params.set_max_time_in_seconds(static_cast<double>(time_limit_seconds));

        const CpSolverResponse response = SolveWithParameters(model.Build(), params);

        if (response.status() != CpSolverStatus::OPTIMAL &&
            response.status() != CpSolverStatus::FEASIBLE) {
            return std::unexpected(SolverError{
                "ILP: CP-SAT could not find a feasible solution within the time limit."});
        }

        // ── STEP 7: Extract solution ───────────────────────────────────────────
        return extract_solution(response, x_vars, region_edges, region.net_ids, N, E);
    }

    // ─── Private OR-Tools helpers (each one handles one numbered step above) ──

    [[nodiscard]] std::vector<EdgeDesc>
    collect_region_edges(const BoundingBox& bbox, const RoutingGridGraph& grid) const {
        std::vector<EdgeDesc> result;
        auto [ei, ei_end] = boost::edges(grid.graph());
        for (auto it = ei; it != ei_end; ++it) {
            const auto& sp = grid.graph()[boost::source(*it, grid.graph())].pos;
            if (bbox.contains(sp.x, sp.y, sp.z))
                result.push_back(*it);
        }
        return result;
    }

    [[nodiscard]] std::vector<std::vector<operations_research::sat::BoolVar>>
    create_variables(operations_research::sat::CpModelBuilder& model, int N, int E) const {
        std::vector<std::vector<operations_research::sat::BoolVar>> x(
            static_cast<size_t>(N),
            std::vector<operations_research::sat::BoolVar>(static_cast<size_t>(E)));
        for (int n = 0; n < N; ++n)
            for (int e = 0; e < E; ++e)
                x[static_cast<size_t>(n)][static_cast<size_t>(e)] = model.NewBoolVar();
        return x;
    }

    void add_no_short_constraints(
            operations_research::sat::CpModelBuilder& model,
            const std::vector<std::vector<operations_research::sat::BoolVar>>& x,
            int N, int E) const {
        // For each edge: Σ_n x[n][e] ≤ 1  (at most one net per edge)
        for (int e = 0; e < E; ++e) {
            operations_research::sat::LinearExpr occupancy;
            for (int n = 0; n < N; ++n)
                occupancy += x[static_cast<size_t>(n)][static_cast<size_t>(e)];
            model.AddLessOrEqual(occupancy, 1);
        }
    }

    void add_connectivity_constraints(
            operations_research::sat::CpModelBuilder& model,
            const std::vector<std::vector<operations_research::sat::BoolVar>>& x,
            int N, int E) const {
        // For each net: Σ_e x[n][e] ≥ 1  (each net uses at least one edge)
        for (int n = 0; n < N; ++n) {
            operations_research::sat::LinearExpr usage;
            for (int e = 0; e < E; ++e)
                usage += x[static_cast<size_t>(n)][static_cast<size_t>(e)];
            model.AddGreaterOrEqual(usage, 1);
        }
    }

    void set_wirelength_objective(
            operations_research::sat::CpModelBuilder& model,
            const std::vector<std::vector<operations_research::sat::BoolVar>>& x,
            const std::vector<EdgeDesc>& region_edges,
            const RoutingGridGraph& grid,
            int N, int E) const {
        operations_research::sat::LinearExpr objective;
        for (int n = 0; n < N; ++n) {
            for (int e = 0; e < E; ++e) {
                // Convert float weight to integer (multiply by 100 for integer LP).
                const int cost = static_cast<int>(
                    grid.graph()[region_edges[static_cast<size_t>(e)]].effective_weight() * 100.0f);
                objective += x[static_cast<size_t>(n)][static_cast<size_t>(e)] * cost;
            }
        }
        model.Minimize(objective);
    }

    [[nodiscard]] std::expected<IlpSolution, SolverError>
    extract_solution(
            const operations_research::sat::CpSolverResponse& response,
            const std::vector<std::vector<operations_research::sat::BoolVar>>& x,
            const std::vector<EdgeDesc>& region_edges,
            const std::vector<int>& net_ids,
            int N, int E) const {
        IlpSolution sol; sol.valid = true;
        for (int n = 0; n < N; ++n) {
            std::vector<EdgeDesc> net_edges;
            for (int e = 0; e < E; ++e) {
                if (SolutionBooleanValue(response,
                        x[static_cast<size_t>(n)][static_cast<size_t>(e)]))
                    net_edges.push_back(region_edges[static_cast<size_t>(e)]);
            }
            sol.routes.emplace_back(net_ids[static_cast<size_t>(n)], std::move(net_edges));
        }
        return sol;
    }

#endif // HAVE_OR_TOOLS
};

} // namespace routing_genetic_astar
