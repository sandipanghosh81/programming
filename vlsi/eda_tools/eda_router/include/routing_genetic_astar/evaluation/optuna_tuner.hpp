#pragma once
// Python bindings via pybind11 for Optuna offline meta-tuning.
// [The Formula-1 Pit Crew Analogy]: Between full routing runs, the Optuna Tuner
// acts like the pit crew analysing telemetry from the last race.  It adjusts the
// GA mutation rate (tyre pressure), W_cong initial value (fuel load), and the
// StrategyComposer fanout thresholds (wing angle) so the next run is faster.
#ifdef HAVE_PYBIND11
#  include <pybind11/pybind11.h>
#  include <pybind11/stl.h>
namespace py = pybind11;
#endif
#include "routing_genetic_astar/planner/global_planner.hpp"
#include "routing_genetic_astar/core/history_cost_updater.hpp"

namespace routing_genetic_astar {

struct TunerResult {
    float best_ga_mutation_delta{2.0f};
    float best_w_cong_initial{1.5f};
    int   best_population_size{50};
};

class OptunaTuner {
public:
#ifdef HAVE_PYBIND11
    // Invoke Python Optuna via pybind11 to tune hyperparameters between runs.
    // If the study is missing or keys differ, returns defaults (no throw across boundary).
    [[nodiscard]] TunerResult tune(const std::string& study_name, int /*n_trials*/ = 20) {
        TunerResult fallback{};
        try {
            py::gil_scoped_acquire gil;
            py::module_ optuna = py::module_::import("optuna");
            py::object  study  = optuna.attr("load_study")(
                py::arg("study_name") = study_name,
                py::arg("storage")    = "sqlite:///optuna_vlsi.db");
            py::dict bp = study.attr("best_params").cast<py::dict>();
            TunerResult res = fallback;
            if (bp.contains("mutation_delta"))
                res.best_ga_mutation_delta = bp["mutation_delta"].cast<float>();
            if (bp.contains("w_cong_initial"))
                res.best_w_cong_initial = bp["w_cong_initial"].cast<float>();
            if (bp.contains("population_size"))
                res.best_population_size = bp["population_size"].cast<int>();
            return res;
        } catch (...) {
            return fallback;
        }
    }
#else
    // Fallback when pybind11 is not linked: identity values (no-op tuning).
    [[nodiscard]] TunerResult tune(const std::string& /*study_name*/,
                                   int /*n_trials*/ = 20) {
        return TunerResult{};
    }
#endif

    // Apply tuned parameters to GlobalPlanner and HistoryCostUpdater before the next run.
    void apply(const TunerResult& r, GlobalPlanner& gp, HistoryCostUpdater& hcu) {
        gp.mutation_delta  = r.best_ga_mutation_delta;
        gp.population_size = r.best_population_size;
        hcu.set_w_cong(r.best_w_cong_initial);
    }
};

} // namespace routing_genetic_astar
