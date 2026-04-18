/*
 * via_expander.cpp  —  Two-stage via geometry expander implementation
 * See via_expander.h for design notes and the two-stage architecture.
 */

#include "routing_genetic_astar/via_expander.h"
#include "eda_router/binary_delta_writer.h"
#include "eda_router/oasis_writer.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace eda {

// ─── ViaLibrary ───────────────────────────────────────────────────────────────
std::expected<ViaLibrary, DeltaError>
ViaLibrary::from_json(const nlohmann::json& j) {
    ViaLibrary lib;
    if (!j.contains("via_defs") || !j["via_defs"].is_array()) {
        return std::unexpected(DeltaError{"JSON missing 'via_defs' array"});
    }
    for (const auto& jv : j["via_defs"]) {
        ViaDefinition def;
        def.name        = jv.value("name",       "");
        def.cut_layer   = jv.value("cut_layer",  0);
        def.cut_purpose = jv.value("cut_purpose",0);
        def.cut_w       = jv.value("cut_w",      0);
        def.cut_h       = jv.value("cut_h",      0);
        def.m1_layer    = jv.value("m1_layer",   0);
        def.m2_layer    = jv.value("m2_layer",   0);
        def.enc_m1_x    = jv.value("enc_m1_x",  0);
        def.enc_m1_y    = jv.value("enc_m1_y",  0);
        def.enc_m2_x    = jv.value("enc_m2_x",  0);
        def.enc_m2_y    = jv.value("enc_m2_y",  0);
        def.pitch_x     = jv.value("pitch_x",   0);
        def.pitch_y     = jv.value("pitch_y",   0);
        def.max_cols    = jv.value("max_cols",   1);
        def.max_rows    = jv.value("max_rows",   1);
        def.min_area_m1 = jv.value("min_area_m1",0);
        def.stacks_to   = jv.value("stacks_to", "");

        if (def.name.empty()) continue;  // skip malformed entries

        uint32_t key = layer_key(def.m1_layer, def.m2_layer);
        // Prefer smaller cut to fill layer_pair_map (conservative choice)
        if (!lib.layer_pair_map_.count(key)) {
            lib.layer_pair_map_[key] = def.name;
        }
        lib.defs_[def.name] = std::move(def);
    }
    return lib;
}

const ViaDefinition* ViaLibrary::find(std::string_view name) const {
    auto it = defs_.find(std::string(name));
    return it != defs_.end() ? &it->second : nullptr;
}

const ViaDefinition* ViaLibrary::best_for_layers(int from_layer, int to_layer) const {
    // Try both orderings (router may not guarantee ordering)
    for (auto key : {layer_key(from_layer, to_layer), layer_key(to_layer, from_layer)}) {
        auto it = layer_pair_map_.find(key);
        if (it != layer_pair_map_.end()) {
            auto dit = defs_.find(it->second);
            if (dit != defs_.end()) return &dit->second;
        }
    }
    return nullptr;
}

// ─── ViaExpander ──────────────────────────────────────────────────────────────
ViaExpander::ViaExpander(const ViaLibrary& lib, Config cfg)
    : lib_(lib), cfg_(cfg) {}

ExpandedVia ViaExpander::expand(const ViaFootprint& fp) const {
    const ViaDefinition* def = nullptr;
    if (!fp.via_def_name.empty()) {
        def = lib_.find(fp.via_def_name);
    }
    if (!def) {
        def = lib_.best_for_layers(fp.from_layer, fp.to_layer);
    }
    if (!def) {
        ExpandedVia ev;
        ev.has_drc_violation = true;
        ev.violation_detail  = "No via definition for layers "
                               + std::to_string(fp.from_layer) + "→"
                               + std::to_string(fp.to_layer);
        return ev;
    }
    return expand_single(*def, fp.x, fp.y, fp.array_cols, fp.array_rows);
}

ExpandedVia ViaExpander::expand_single(const ViaDefinition& def,
                                        int32_t cx, int32_t cy,
                                        int cols, int rows) const {
    ExpandedVia ev;

    // Clamp array to what the definition allows
    cols = std::clamp(cols, 1, def.max_cols);
    rows = std::clamp(rows, 1, def.max_rows);

    // Array pitch (fall back to cut_w + min_spacing if pitch not set)
    int pitch_x = def.pitch_x > 0 ? def.pitch_x : def.cut_w + 40;
    int pitch_y = def.pitch_y > 0 ? def.pitch_y : def.cut_h + 40;

    // Origin of cut array (centred on cx, cy)
    int32_t array_total_w = (cols - 1) * pitch_x + def.cut_w;
    int32_t array_total_h = (rows - 1) * pitch_y + def.cut_h;
    int32_t ox = cx - array_total_w / 2;
    int32_t oy = cy - array_total_h / 2;

    // ── Cut layer shapes ───────────────────────────────────────────────────
    for (int c = 0; c < cols; ++c) {
        for (int r = 0; r < rows; ++r) {
            ViaRect cut;
            cut.layer   = def.cut_layer;
            cut.purpose = def.cut_purpose;
            cut.x = ox + c * pitch_x;
            cut.y = oy + r * pitch_y;
            cut.w = def.cut_w;
            cut.h = def.cut_h;
            ev.rects.push_back(cut);
        }
    }

    // ── Metal enclosure shapes (one big rect per layer covering all cuts) ──
    auto make_enc = [&](int layer, int enc_x, int enc_y) {
        ViaRect enc;
        enc.layer   = layer;
        enc.purpose = 0;
        enc.x = ox - enc_x;
        enc.y = oy - enc_y;
        enc.w = array_total_w + 2 * enc_x;
        enc.h = array_total_h + 2 * enc_y;
        ev.rects.push_back(enc);
    };
    make_enc(def.m1_layer, def.enc_m1_x, def.enc_m1_y);
    make_enc(def.m2_layer, def.enc_m2_x, def.enc_m2_y);

    // ── Min-area check ─────────────────────────────────────────────────────
    if (cfg_.check_min_area && !check_min_area(def, cols, rows)) {
        ev.has_drc_violation = true;
        ev.violation_detail  = "M1 enclosure below min_area for "
                               + def.name + " 1x1 array";
    }

    return ev;
}

bool ViaExpander::check_min_area(const ViaDefinition& def, int cols, int rows) const {
    if (def.min_area_m1 == 0) return true;
    int w = (cols - 1) * (def.pitch_x > 0 ? def.pitch_x : def.cut_w + 40)
            + def.cut_w + 2 * def.enc_m1_x;
    int h = (rows - 1) * (def.pitch_y > 0 ? def.pitch_y : def.cut_h + 40)
            + def.cut_h + 2 * def.enc_m1_y;
    return (w * h) >= def.min_area_m1;
}

// ─── Bulk expand + write ──────────────────────────────────────────────────────
std::vector<ViaViolation>
ViaExpander::expand_and_write(std::span<const ViaFootprint> footprints,
                               BinaryDeltaWriter& delta,
                               OASISWriter&       oasis) const {
    std::vector<ViaViolation> violations;

    for (const auto& fp : footprints) {
        ExpandedVia ev = expand(fp);

        if (ev.has_drc_violation) {
            violations.push_back({fp.x, fp.y, fp.via_def_name,
                                  "geometry", ev.violation_detail});
        }

        for (const auto& r : ev.rects) {
            // Write to delta (for the HEA Apply EU)
            delta.add_rect(static_cast<uint16_t>(r.layer),
                           static_cast<uint16_t>(r.purpose),
                           r.x, r.y, r.w, r.h);
            // Write to OASIS (for the KLayout DRC gate)
            oasis.write_rect(r.layer, r.purpose, r.x, r.y, r.w, r.h);
        }
    }

    return violations;
}

}  // namespace eda
