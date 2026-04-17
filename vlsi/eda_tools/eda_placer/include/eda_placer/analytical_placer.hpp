#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// analytical_placer — fast row-based placement + HPWL accounting
//
// [Warehouse Shelf Analogy]: cells are pallets slid onto fixed shelf rows; we only
// measure how far extension cords (nets) must stretch between pallets — that total
// half-perimeter distance is HPWL, the canonical wirelength proxy at placement time.
// ═══════════════════════════════════════════════════════════════════════════════

#include <cstdint>
#include <vector>

#include "eda_router/io/def_design_loader.hpp"
#include "eda_router/types.hpp"

namespace eda_placer {

struct PlacerResult {
    int          overlap_count{0};
    double       hpwl_dbu{0.0};
    std::int64_t runtime_ms{0};
};

// Places every cell on a deterministic row-major grid (legal, no overlaps if
// capacity permits) and computes half-perimeter wirelength of all nets using pin
// bbox in DBU space (linear map from grid coordinates).
[[nodiscard]] PlacerResult place_row_major_and_hpwl(
    std::vector<eda_router::CellPlacement>& cells,
    const eda_router::DesignSummary&        summary,
    std::int64_t die_xlo,
    std::int64_t die_ylo,
    std::int64_t die_xhi,
    std::int64_t die_yhi,
    int          rows,
    int          cols);

} // namespace eda_placer
