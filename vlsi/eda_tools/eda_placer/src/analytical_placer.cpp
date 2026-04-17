#include "eda_placer/analytical_placer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace eda_placer {
namespace {

[[nodiscard]] double grid_to_dbu_x(int gx, std::int64_t die_xlo, std::int64_t die_xhi, int cols) {
    const double w = static_cast<double>(std::max<std::int64_t>(1, die_xhi - die_xlo));
    const double t = static_cast<double>(gx) / static_cast<double>(std::max(1, cols - 1));
    return static_cast<double>(die_xlo) + t * w;
}

[[nodiscard]] double grid_to_dbu_y(int gy, std::int64_t die_ylo, std::int64_t die_yhi, int rows) {
    const double h = static_cast<double>(std::max<std::int64_t>(1, die_yhi - die_ylo));
    const double t = static_cast<double>(gy) / static_cast<double>(std::max(1, rows - 1));
    return static_cast<double>(die_ylo) + t * h;
}

} // namespace

PlacerResult place_row_major_and_hpwl(
    std::vector<eda_router::CellPlacement>& cells,
    const eda_router::DesignSummary&        summary,
    std::int64_t die_xlo,
    std::int64_t die_ylo,
    std::int64_t die_xhi,
    std::int64_t die_yhi,
    int          rows,
    int          cols) {

    const auto t0 = std::chrono::steady_clock::now();

    // Row-major shelf packing: each cell gets a unique grid slot when possible.
    int slot = 0;
    for (auto& c : cells) {
        const int inner_cols = std::max(1, cols - 2);
        const int inner_rows = std::max(1, rows - 2);
        c.grid_x = 1 + (slot % inner_cols);
        c.grid_y = 1 + (slot / inner_cols) % inner_rows;
        c.x_dbu  = static_cast<std::int64_t>(std::lround(grid_to_dbu_x(c.grid_x, die_xlo, die_xhi, cols)));
        c.y_dbu  = static_cast<std::int64_t>(std::lround(grid_to_dbu_y(c.grid_y, die_ylo, die_yhi, rows)));
        ++slot;
    }

    // Overlap check (same grid slot — should not happen with unique slots).
    int overlaps = 0;
    for (size_t i = 0; i < cells.size(); ++i)
        for (size_t j = i + 1; j < cells.size(); ++j)
            if (cells[i].grid_x == cells[j].grid_x && cells[i].grid_y == cells[j].grid_y)
                ++overlaps;

    double hpwl = 0.0;
    for (const auto& net : summary.nets) {
        if (net.pins.empty()) continue;
        double minx = grid_to_dbu_x(net.pins[0].x, die_xlo, die_xhi, cols);
        double maxx = minx;
        double miny = grid_to_dbu_y(net.pins[0].y, die_ylo, die_yhi, rows);
        double maxy = miny;
        for (const auto& p : net.pins) {
            const double x = grid_to_dbu_x(p.x, die_xlo, die_xhi, cols);
            const double y = grid_to_dbu_y(p.y, die_ylo, die_yhi, rows);
            minx = std::min(minx, x);
            maxx = std::max(maxx, x);
            miny = std::min(miny, y);
            maxy = std::max(maxy, y);
        }
        hpwl += (maxx - minx) + (maxy - miny);
    }

    const auto t1 = std::chrono::steady_clock::now();
    PlacerResult r;
    r.overlap_count = overlaps;
    r.hpwl_dbu      = hpwl;
    r.runtime_ms    = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    return r;
}

} // namespace eda_placer
