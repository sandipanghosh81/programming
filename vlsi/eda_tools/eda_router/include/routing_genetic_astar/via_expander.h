#pragma once
/*
 * via_expander.h  —  Two-stage via geometry expander
 * ─────────────────────────────────────────────────────────────────────────────
 * STAGE 1 (topology planner — upstream in routing_pipeline):
 *   The router places a ViaFootprint at each layer transition point.
 *   A ViaFootprint is just (x, y, from_layer, to_layer, via_def_name).
 *   It does NOT contain geometry — only the intent to connect two layers.
 *
 * STAGE 2 (THIS FILE — via expander):
 *   Given a list of ViaFootprints + a ViaLibrary (parsed from JSON techfile
 *   export), the expander generates the full 3-layer geometry:
 *     - Cut layer shape(s) at (x, y)
 *     - Metal-1 enclosure rectangle (bottom layer)
 *     - Metal-2 enclosure rectangle (top layer)
 *   It also handles:
 *     - Asymmetric enclosures (different overhang per edge per rule)
 *     - Via arrays (larger current → 2×2, 3×3 arrays)
 *     - Via stacking (M1→M2→M3 automatically expanded)
 *     - Local metal clearance checks (marks violations, does not abort)
 *
 * OUTPUT:
 *   ExpandedVia — all rectangles needed to draw the via in the database.
 *   Can be serialised directly to BinaryDeltaWriter or OASISWriter.
 *
 * JSON VIA TECH FORMAT (subset that matters here):
 *   {
 *     "via_defs": [{
 *       "name": "V12",
 *       "cut_layer": 15, "cut_purpose": 0,
 *       "cut_w": 140, "cut_h": 140,
 *       "m1_layer": 10, "m2_layer": 20,
 *       "enc_m1_x": 55, "enc_m1_y": 35,   // asymmetric OK
 *       "enc_m2_x": 55, "enc_m2_y": 35,
 *       "pitch_x": 340, "pitch_y": 340,    // for arrays
 *       "max_cols": 4,  "max_rows": 4,
 *       "min_area_m1": 35000,              // nm² min-area rule
 *       "stacks_to": "V23"                 // for automatic stacking
 *     }]
 *   }
 */

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <unordered_map>
#include <expected>
#include <nlohmann/json.hpp>

namespace eda {

struct DeltaError;       // forward
class BinaryDeltaWriter;
class OASISWriter;

// ─── Via definition (parsed from JSON) ────────────────────────────────────────
struct ViaDefinition {
    std::string name;
    int  cut_layer  = 0;  int  cut_purpose  = 0;
    int  cut_w      = 0;  int  cut_h        = 0;
    int  m1_layer   = 0;  int  m2_layer     = 0;
    int  enc_m1_x   = 0;  int  enc_m1_y     = 0;
    int  enc_m2_x   = 0;  int  enc_m2_y     = 0;
    int  pitch_x    = 0;  int  pitch_y      = 0;
    int  max_cols   = 1;  int  max_rows     = 1;
    int  min_area_m1 = 0;
    std::string stacks_to;   // name of via def for next layer pair (or "")
};

// ─── Via library (collection of all via definitions) ─────────────────────────
class ViaLibrary {
public:
    [[nodiscard]] static std::expected<ViaLibrary, DeltaError>
        from_json(const nlohmann::json& j);

    [[nodiscard]] const ViaDefinition* find(std::string_view name) const;
    [[nodiscard]] const ViaDefinition* best_for_layers(int from_layer, int to_layer) const;

private:
    std::unordered_map<std::string, ViaDefinition> defs_;
    // layer-pair → best single-cut via
    std::unordered_map<uint32_t, std::string>      layer_pair_map_;

    static uint32_t layer_key(int a, int b) noexcept {
        return (static_cast<uint32_t>(a) << 16) | static_cast<uint32_t>(b);
    }
};

// ─── Via footprint (stage 1 output) ──────────────────────────────────────────
struct ViaFootprint {
    int32_t x          = 0;
    int32_t y          = 0;
    int     from_layer = 0;
    int     to_layer   = 0;
    std::string via_def_name;   // "" → auto-select from library
    int     array_cols = 1;     // 1 = single via
    int     array_rows = 1;
};

// ─── Expanded via (stage 2 output) ───────────────────────────────────────────
struct ViaRect {
    int layer   = 0;
    int purpose = 0;
    int32_t x   = 0;
    int32_t y   = 0;
    int32_t w   = 0;
    int32_t h   = 0;
};

struct ExpandedVia {
    std::vector<ViaRect> rects;   // all shapes (cut + enclosures)
    bool has_drc_violation = false;
    std::string violation_detail;
};

// ─── Violation record for reporting ──────────────────────────────────────────
struct ViaViolation {
    int32_t x, y;
    std::string via_def_name;
    std::string rule;
    std::string detail;
};

// ─── Expander ─────────────────────────────────────────────────────────────────
class ViaExpander {
public:
    struct Config {
        bool allow_stacking   = true;   // expand stacked vias automatically
        bool check_min_area   = true;   // flag min-area violations
        float array_threshold = 3.0f;   // prefer array if current_ua > threshold
    };

    explicit ViaExpander(const ViaLibrary& lib, Config cfg = {});

    /// Expand a single ViaFootprint into full 3-layer geometry.
    [[nodiscard]] ExpandedVia expand(const ViaFootprint& fp) const;

    /// Expand all footprints and write directly to the delta file.
    /// Returns violation list (non-fatal).
    std::vector<ViaViolation>
        expand_and_write(std::span<const ViaFootprint> footprints,
                         BinaryDeltaWriter& delta,
                         OASISWriter&       oasis) const;

private:
    [[nodiscard]] ExpandedVia expand_single(const ViaDefinition& def,
                                             int32_t x, int32_t y,
                                             int cols, int rows) const;
    [[nodiscard]] bool check_min_area(const ViaDefinition& def,
                                       int cols, int rows) const;

    const ViaLibrary& lib_;
    Config            cfg_;
};

}  // namespace eda
