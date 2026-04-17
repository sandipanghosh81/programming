#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eda_placer::analog {

// All geometry units are "layout units" (tool neutral). Typically microns.
using coord_t = double;

struct Point {
    coord_t x{0};
    coord_t y{0};
};

struct Rect {
    coord_t x{0};
    coord_t y{0};
    coord_t w{0};
    coord_t h{0};
};

enum class Orientation : std::uint8_t {
    R0,
    R180,
    MX,
    MY,
};

struct Pin {
    std::string name;
    // Offset from instance origin (lower-left) in the instance's local coords (pre-orient).
    Point offset{};
};

struct Variant {
    std::string name;
    coord_t w{0};
    coord_t h{0};
    std::vector<Pin> pins{};
    // Allowed orientations for this variant.
    std::vector<Orientation> allowed_orientations{};
};

struct Instance {
    std::string id;
    std::string device_type;
    std::vector<Variant> variants{};
    // Optional: fixed placement (e.g., IOs, hard IP).
    bool fixed{false};
    Rect fixed_rect{};
};

struct NetPinRef {
    std::string inst_id;
    std::string pin_name;
};

struct Net {
    std::string name;
    double      weight{1.0};
    std::vector<NetPinRef> pins{};
};

struct SymmetryPair {
    std::string a;
    std::string b;
};

struct SymmetrySpec {
    // For now, we support one vertical symmetry axis and a list of mirrored pairs.
    // Extensions: multiple axes, groups, common-centroid templates, etc.
    bool vertical{true};
    // If not provided, the placer will choose a good axis (typically outline_w/2).
    bool   has_axis{false};
    coord_t axis{0};
    std::vector<SymmetryPair> pairs{};
};

struct Outline {
    coord_t w{0};
    coord_t h{0};
};

struct Problem {
    Outline outline{};
    std::vector<Instance> instances{};
    std::vector<Net> nets{};
    SymmetrySpec symmetry{};
};

struct PlacedInstance {
    std::string id;
    int         variant_idx{-1};
    Orientation orient{Orientation::R0};
    Rect        rect{};
};

struct Metrics {
    double area_used{0.0};
    double wl_est{0.0};
    double congestion_est{0.0};
    double symmetry_imbalance{0.0};
    int    overlaps{0};
    std::int64_t runtime_ms{0};
};

struct Result {
    std::vector<PlacedInstance> placed{};
    Metrics metrics{};
};

struct Options {
    // SA / local search controls.
    int    seed{1};
    int    iters{20000};
    double init_temp{1.0};
    double final_temp{1e-3};

    // Objective weights.
    double w_area{1.0};
    double w_wl{1.0};
    double w_cong{0.5};
    double w_sym{5.0};
    double w_overlap{1000.0}; // hard-ish

    // Routing estimator grid.
    int cong_bins_x{48};
    int cong_bins_y{48};
};

} // namespace eda_placer::analog

