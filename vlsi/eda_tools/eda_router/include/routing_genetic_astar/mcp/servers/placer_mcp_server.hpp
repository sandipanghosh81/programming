#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// PlacerMcpServer — JSON-RPC place_cells → eda_placer analytical engine
// ═══════════════════════════════════════════════════════════════════════════════

#include <memory>
#include <fstream>
#include <nlohmann/json.hpp>

#include "eda_placer/analytical_placer.hpp"
#include "eda_placer/analog_placer.hpp"
#include "routing_genetic_astar/shared_database.hpp"

namespace mcp {

class PlacerMcpServer {
public:
    explicit PlacerMcpServer(std::shared_ptr<SharedDatabase> db) : db_(std::move(db)) {}

    // PARAMS:
    // - Digital (back-compat): optional { "strategy": "row_major" }
    // - Analog (tool-neutral):
    //     { "analog_problem": { ... schema in eda_placer/analog_placer.hpp ... }, "options": {...} }
    //   or
    //     { "problem_path": "/abs/or/rel/path/problem.json", "options": {...} }
    [[nodiscard]] nlohmann::json place_cells(const nlohmann::json& params) {
        // ── Analog path: bypass SharedDatabase and place from tool-neutral JSON ──
        try {
            if (params.contains("analog_problem") || params.contains("problem_path")) {
                nlohmann::json prob;
                if (params.contains("analog_problem")) {
                    prob = params.at("analog_problem");
                } else {
                    const auto path = params.at("problem_path").get<std::string>();
                    std::ifstream f(path);
                    if (!f) return {{"error", "Cannot open problem_path: " + path}};
                    f >> prob;
                }

                const auto p = eda_placer::analog::problem_from_json(prob);
                const auto o = eda_placer::analog::options_from_json(params);
                const auto r = eda_placer::analog::place(p, o);
                auto out = eda_placer::analog::result_to_json(r);
                out["strategy"] = "analog_sa_rudy_symmetry";
                return out;
            }
        } catch (const std::exception& e) {
            return {{"error", std::string("Analog placement failed: ") + e.what()}};
        }

        // ── Digital path (existing behavior) ───────────────────────────────────
        if (!db_->is_loaded) {
            return {{"error", "No design loaded. Call load_design first, or pass analog_problem/problem_path."}};
        }
        if (db_->cell_placements.empty()) {
            return {
                {"overlap_count", 0},
                {"hpwl",          0.0},
                {"runtime_ms",    0},
                {"note",          "No cells to place — DEF had no COMPONENTS"},
            };
        }

        const auto res = eda_placer::place_row_major_and_hpwl(
            db_->cell_placements,
            db_->design_summary,
            db_->die_xlo,
            db_->die_ylo,
            db_->die_xhi,
            db_->die_yhi,
            db_->routing_grid.rows(),
            db_->routing_grid.cols());

        return {
            {"overlap_count", res.overlap_count},
            {"hpwl",          res.hpwl_dbu},
            {"runtime_ms",    res.runtime_ms},
            {"cells_placed",  static_cast<int>(db_->cell_placements.size())},
            {"strategy",      "row_major_analytical"},
        };
    }

private:
    std::shared_ptr<SharedDatabase> db_;
};

} // namespace mcp
