// ═══════════════════════════════════════════════════════════════════════════════
// FILE: routing_genetic_astar/include/routing_genetic_astar/mcp/servers/db_mcp_server.hpp
// PURPOSE: Database MCP server — handles all "db.*" and "load_design" methods.
// ═══════════════════════════════════════════════════════════════════════════════
#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "routing_genetic_astar/io/def_design_loader.hpp"
#include "routing_genetic_astar/shared_database.hpp"

namespace mcp {

class DbMcpServer {
public:
    explicit DbMcpServer(std::shared_ptr<SharedDatabase> db) : db_(db) {}

    [[nodiscard]] nlohmann::json load_design(const nlohmann::json& params) {
        const std::string filename = params.value("filename", "unknown.def");
        using namespace routing_genetic_astar;

        auto ends_with = [](const std::string& f, std::string_view suf) {
            return f.size() >= suf.size()
                && std::equal(suf.rbegin(), suf.rend(), f.rbegin(),
                             [](char a, char b) {
                                 return std::tolower(static_cast<unsigned char>(a))
                                     == std::tolower(static_cast<unsigned char>(b));
                             });
        };

        const bool try_def = ends_with(filename, ".def");
        if (try_def) {
            std::ifstream probe(filename);
            if (!probe.good()) {
                return {{"error", "Cannot open DEF file: " + filename}};
            }
            probe.close();

            auto parsed = parse_def_file(filename);
            if (!parsed) {
                return {{"error", std::string("DEF parse: ") + parsed.error().message}};
            }
            ParsedDefDesign design = std::move(*parsed);

            const std::int64_t w_dbu = std::max<std::int64_t>(1, design.die_xhi - design.die_xlo);
            const std::int64_t h_dbu = std::max<std::int64_t>(1, design.die_yhi - design.die_ylo);
            // [Goldilocks grid]: not so coarse that routing misses detail, not so fine that we
            // allocate millions of vertices — scale roughly with die size in thousands of DBU.
            int cols = static_cast<int>(std::clamp(w_dbu / 1500, static_cast<std::int64_t>(48),
                                                    static_cast<std::int64_t>(384)));
            int rows = static_cast<int>(std::clamp(h_dbu / 1500, static_cast<std::int64_t>(48),
                                                   static_cast<std::int64_t>(384)));
            const int layers = 6;
            std::vector<LayerConfig> lcfg(static_cast<size_t>(layers));
            for (int l = 0; l < layers; ++l) {
                lcfg[static_cast<size_t>(l)].preferred_track_weight = (l % 2 == 0) ? 1.0f : 1.2f;
                lcfg[static_cast<size_t>(l)].via_penalty = 2.0f;
            }

            db_->routing_grid.build_lattice(rows, cols, layers, lcfg);
            snap_to_grid(design, rows, cols);

            db_->design_summary   = std::move(design.summary);
            db_->cell_placements  = std::move(design.cells);
            db_->die_xlo          = design.die_xlo;
            db_->die_ylo          = design.die_ylo;
            db_->die_xhi          = design.die_xhi;
            db_->die_yhi          = design.die_yhi;
            db_->dbu_per_micron   = design.dbu_per_micron;
            db_->is_loaded        = true;
            db_->design_name      = design.design_name.empty() ? filename : design.design_name;
            db_->num_nets         = db_->design_summary.total_nets;
            db_->num_cells        = static_cast<int>(db_->cell_placements.size());

            const auto v_count = boost::num_vertices(db_->routing_grid.graph());
            const auto e_count = boost::num_edges(db_->routing_grid.graph());

            return {
                {"vertices",    v_count},
                {"edges",       e_count},
                {"design_name", db_->design_name},
                {"layers",      layers},
                {"rows",        rows},
                {"cols",        cols},
                {"source",      "def"},
            };
        }

        // ── Synthetic demo (non-DEF): still builds a legal lattice for routing smoke tests ──
        const int rows = 100, cols = 100, layers = 6;
        std::vector<LayerConfig> lcfg(static_cast<size_t>(layers));
        for (int l = 0; l < layers; ++l) {
            lcfg[static_cast<size_t>(l)].preferred_track_weight = (l % 2 == 0) ? 1.0f : 1.2f;
            lcfg[static_cast<size_t>(l)].via_penalty = 2.0f;
        }
        db_->routing_grid.build_lattice(rows, cols, layers, lcfg);

        db_->design_summary  = {};
        db_->cell_placements.clear();
        db_->die_xlo = 0;
        db_->die_ylo = 0;
        db_->die_xhi = 100000;
        db_->die_yhi = 100000;
        db_->dbu_per_micron = 1000;
        for (int i = 0; i < 12; ++i) {
            NetDefinition n;
            n.name = "net_" + std::to_string(i);
            n.id   = i;
            n.pins = {GridPoint{10 + i, 10 + i, 0}, GridPoint{50 + i, 50 + i, 0}};
            db_->design_summary.nets.push_back(std::move(n));
        }
        db_->design_summary.total_nets  = static_cast<int>(db_->design_summary.nets.size());
        db_->design_summary.total_pins    = 0;
        for (const auto& n : db_->design_summary.nets)
            db_->design_summary.total_pins += static_cast<int>(n.pins.size());

        db_->is_loaded   = true;
        db_->design_name = filename;
        db_->num_nets    = db_->design_summary.total_nets;
        db_->num_cells   = 8;
        for (int i = 0; i < 8; ++i) {
            CellPlacement c;
            c.inst_name = "u" + std::to_string(i);
            c.ref_name  = "GATE";
            c.x_dbu     = 5000 * i;
            c.y_dbu     = 5000 * i;
            c.grid_x    = std::min(cols - 1, 5 + i * 10);
            c.grid_y    = std::min(rows - 1, 5 + i * 10);
            db_->cell_placements.push_back(std::move(c));
        }

        const auto v_count = boost::num_vertices(db_->routing_grid.graph());
        const auto e_count = boost::num_edges(db_->routing_grid.graph());

        return {
            {"vertices",    v_count},
            {"edges",       e_count},
            {"design_name", filename},
            {"layers",      layers},
            {"rows",        rows},
            {"cols",        cols},
            {"source",      "synthetic"},
        };
    }

    [[nodiscard]] nlohmann::json status() const {
        return {
            {"is_loaded",   db_->is_loaded},
            {"design_name", db_->design_name},
            {"num_nets",    db_->num_nets},
            {"num_cells",   db_->num_cells},
        };
    }

    [[nodiscard]] nlohmann::json query_nets() const {
        if (!db_->is_loaded) return {{"nets", nlohmann::json::array()}};
        nlohmann::json nets = nlohmann::json::array();
        for (const auto& n : db_->design_summary.nets) {
            nets.push_back({
                {"id",   n.id},
                {"name", n.name},
                {"pins", static_cast<int>(n.pins.size())},
            });
        }
        return {{"nets", nets}, {"total", static_cast<int>(nets.size())}};
    }

    [[nodiscard]] nlohmann::json query_cells() const {
        if (!db_->is_loaded) return {{"cells", nlohmann::json::array()}};
        nlohmann::json cells = nlohmann::json::array();
        for (const auto& c : db_->cell_placements) {
            cells.push_back({
                {"name",    c.inst_name},
                {"ref",     c.ref_name},
                {"x_dbu",   c.x_dbu},
                {"y_dbu",   c.y_dbu},
                {"grid_x",  c.grid_x},
                {"grid_y",  c.grid_y},
            });
        }
        return {{"cells", cells}, {"total", static_cast<int>(cells.size())}};
    }

    [[nodiscard]] nlohmann::json query_bbox() const {
        if (!db_->is_loaded) return {{"error", "Design not loaded"}};
        return {
            {"x_min", 0},
            {"y_min", 0},
            {"x_max", db_->routing_grid.cols() - 1},
            {"y_max", db_->routing_grid.rows() - 1},
            {"die_xlo_dbu", db_->die_xlo},
            {"die_ylo_dbu", db_->die_ylo},
            {"die_xhi_dbu", db_->die_xhi},
            {"die_yhi_dbu", db_->die_yhi},
        };
    }

    [[nodiscard]] nlohmann::json net_bbox(const nlohmann::json& params) const {
        if (!db_->is_loaded) return {{"error", "Design not loaded"}};
        const std::string net_name = params.value("net_name", "");
        for (const auto& n : db_->design_summary.nets) {
            if (n.name != net_name) continue;
            if (n.pins.empty())
                return {{"bbox", nlohmann::json::array({0, 0, 0, 0})}, {"net_name", net_name}};
            int xmin = n.pins[0].x, ymin = n.pins[0].y;
            int xmax = xmin, ymax = ymin;
            for (const auto& p : n.pins) {
                xmin = std::min(xmin, p.x);
                ymin = std::min(ymin, p.y);
                xmax = std::max(xmax, p.x);
                ymax = std::max(ymax, p.y);
            }
            return {
                {"bbox",     {xmin, ymin, xmax, ymax}},
                {"net_name", net_name},
            };
        }
        return {{"error", "Unknown net: " + net_name}};
    }

private:
    std::shared_ptr<SharedDatabase> db_;
};

} // namespace mcp
