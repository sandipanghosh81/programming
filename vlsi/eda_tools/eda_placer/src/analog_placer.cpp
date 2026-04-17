#include "eda_placer/analog_placer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using json = nlohmann::json;

namespace eda_placer::analog {
namespace {

[[nodiscard]] Orientation parse_orient(const std::string& s) {
    if (s == "R0") return Orientation::R0;
    if (s == "R180") return Orientation::R180;
    if (s == "MX") return Orientation::MX;
    if (s == "MY") return Orientation::MY;
    throw std::runtime_error("Unknown orientation: " + s);
}

[[nodiscard]] std::string orient_to_string(Orientation o) {
    switch (o) {
        case Orientation::R0: return "R0";
        case Orientation::R180: return "R180";
        case Orientation::MX: return "MX";
        case Orientation::MY: return "MY";
    }
    return "R0";
}

[[nodiscard]] Point apply_orient(Point p, coord_t w, coord_t h, Orientation o) {
    // Local coords origin at lower-left of the *unoriented* bbox.
    // Return oriented point offset from the oriented lower-left.
    switch (o) {
        case Orientation::R0:   return p;
        case Orientation::R180: return {w - p.x, h - p.y};
        case Orientation::MX:   return {p.x, h - p.y};
        case Orientation::MY:   return {w - p.x, p.y};
    }
    return p;
}

[[nodiscard]] Rect clamp_to_outline(Rect r, const Outline& o) {
    r.x = std::clamp<coord_t>(r.x, 0.0, std::max<coord_t>(0.0, o.w - r.w));
    r.y = std::clamp<coord_t>(r.y, 0.0, std::max<coord_t>(0.0, o.h - r.h));
    return r;
}

[[nodiscard]] bool rects_overlap(const Rect& a, const Rect& b) {
    const bool sep_x = (a.x + a.w <= b.x) || (b.x + b.w <= a.x);
    const bool sep_y = (a.y + a.h <= b.y) || (b.y + b.h <= a.y);
    return !(sep_x || sep_y);
}

struct IndexedNetPin {
    int inst_idx{-1};
    int pin_idx{-1};
};

struct InstState {
    int         variant_idx{0};
    Orientation orient{Orientation::R0};
    Rect        rect{};
};

[[nodiscard]] coord_t hpwl_for_net(
    const Net& net,
    const std::vector<Instance>& insts,
    const std::vector<InstState>& st,
    const std::unordered_map<std::string, int>& inst_index) {

    coord_t minx = std::numeric_limits<coord_t>::infinity();
    coord_t maxx = -std::numeric_limits<coord_t>::infinity();
    coord_t miny = std::numeric_limits<coord_t>::infinity();
    coord_t maxy = -std::numeric_limits<coord_t>::infinity();
    bool any = false;

    for (const auto& p : net.pins) {
        auto it = inst_index.find(p.inst_id);
        if (it == inst_index.end()) continue;
        const int ii = it->second;
        const auto& inst = insts[ii];
        const auto& is = st[ii];
        if (is.variant_idx < 0 || is.variant_idx >= static_cast<int>(inst.variants.size())) continue;
        const auto& var = inst.variants[is.variant_idx];

        auto pit = std::find_if(var.pins.begin(), var.pins.end(), [&](const Pin& pin) {
            return pin.name == p.pin_name;
        });
        if (pit == var.pins.end()) continue;

        const auto off = apply_orient(pit->offset, var.w, var.h, is.orient);
        const coord_t x = is.rect.x + off.x;
        const coord_t y = is.rect.y + off.y;
        minx = std::min(minx, x);
        maxx = std::max(maxx, x);
        miny = std::min(miny, y);
        maxy = std::max(maxy, y);
        any = true;
    }
    if (!any) return 0.0;
    return (maxx - minx) + (maxy - miny);
}

struct CongGrid {
    int nx{48};
    int ny{48};
    coord_t w{1};
    coord_t h{1};
    std::vector<double> demand; // size nx*ny

    CongGrid(int nx_, int ny_, coord_t w_, coord_t h_) : nx(nx_), ny(ny_), w(w_), h(h_), demand(static_cast<size_t>(nx_ * ny_), 0.0) {}

    void clear() { std::fill(demand.begin(), demand.end(), 0.0); }

    [[nodiscard]] int bx(coord_t x) const {
        const coord_t t = (w <= 0) ? 0.0 : std::clamp(x / w, 0.0, 0.999999);
        return static_cast<int>(t * nx);
    }
    [[nodiscard]] int by(coord_t y) const {
        const coord_t t = (h <= 0) ? 0.0 : std::clamp(y / h, 0.0, 0.999999);
        return static_cast<int>(t * ny);
    }
    void add_box(coord_t x0, coord_t y0, coord_t x1, coord_t y1, double amt) {
        const int ix0 = std::clamp(bx(std::min(x0, x1)), 0, nx - 1);
        const int ix1 = std::clamp(bx(std::max(x0, x1)), 0, nx - 1);
        const int iy0 = std::clamp(by(std::min(y0, y1)), 0, ny - 1);
        const int iy1 = std::clamp(by(std::max(y0, y1)), 0, ny - 1);
        for (int iy = iy0; iy <= iy1; ++iy) {
            for (int ix = ix0; ix <= ix1; ++ix) {
                demand[static_cast<size_t>(iy * nx + ix)] += amt;
            }
        }
    }
    [[nodiscard]] double l2_overflow() const {
        // Tool-neutral: treat capacity as 1.0 per tile and measure squared overflow.
        double s = 0.0;
        for (double d : demand) {
            const double of = std::max(0.0, d - 1.0);
            s += of * of;
        }
        return s;
    }
};

[[nodiscard]] double congestion_cost(
    const Problem& p,
    const std::vector<InstState>& st,
    const std::unordered_map<std::string, int>& inst_index,
    int bins_x,
    int bins_y) {

    CongGrid g{bins_x, bins_y, p.outline.w, p.outline.h};
    g.clear();

    // Very cheap RUDY-like proxy: each net contributes uniformly over its bbox.
    for (const auto& net : p.nets) {
        coord_t minx = std::numeric_limits<coord_t>::infinity();
        coord_t maxx = -std::numeric_limits<coord_t>::infinity();
        coord_t miny = std::numeric_limits<coord_t>::infinity();
        coord_t maxy = -std::numeric_limits<coord_t>::infinity();
        bool any = false;
        for (const auto& pr : net.pins) {
            auto it = inst_index.find(pr.inst_id);
            if (it == inst_index.end()) continue;
            const int ii = it->second;
            const auto& inst = p.instances[ii];
            const auto& is = st[ii];
            if (is.variant_idx < 0 || is.variant_idx >= static_cast<int>(inst.variants.size())) continue;
            const auto& var = inst.variants[is.variant_idx];

            auto pit = std::find_if(var.pins.begin(), var.pins.end(), [&](const Pin& pin) { return pin.name == pr.pin_name; });
            if (pit == var.pins.end()) continue;
            const auto off = apply_orient(pit->offset, var.w, var.h, is.orient);
            const coord_t x = is.rect.x + off.x;
            const coord_t y = is.rect.y + off.y;
            minx = std::min(minx, x);
            maxx = std::max(maxx, x);
            miny = std::min(miny, y);
            maxy = std::max(maxy, y);
            any = true;
        }
        if (!any) continue;
        const coord_t dx = std::max<coord_t>(1e-6, maxx - minx);
        const coord_t dy = std::max<coord_t>(1e-6, maxy - miny);
        const double rudy = (net.weight) / static_cast<double>(dx * dy);
        g.add_box(minx, miny, maxx, maxy, rudy);
    }
    return g.l2_overflow();
}

[[nodiscard]] int overlap_count(const Problem& p, const std::vector<InstState>& st) {
    int c = 0;
    for (size_t i = 0; i < st.size(); ++i) {
        for (size_t j = i + 1; j < st.size(); ++j) {
            if (rects_overlap(st[i].rect, st[j].rect)) ++c;
        }
    }
    // Count outline violations as overlaps too (hard-ish).
    for (size_t i = 0; i < st.size(); ++i) {
        const auto& r = st[i].rect;
        if (r.x < 0 || r.y < 0 || r.x + r.w > p.outline.w || r.y + r.h > p.outline.h) ++c;
    }
    return c;
}

[[nodiscard]] double symmetry_imbalance_cost(
    const Problem& p,
    const std::vector<InstState>& st,
    const std::unordered_map<std::string, int>& inst_index) {

    if (p.symmetry.pairs.empty()) return 0.0;
    const coord_t axis = p.symmetry.has_axis ? p.symmetry.axis : (p.outline.w * 0.5);

    double cost = 0.0;
    for (const auto& sp : p.symmetry.pairs) {
        auto ia = inst_index.find(sp.a);
        auto ib = inst_index.find(sp.b);
        if (ia == inst_index.end() || ib == inst_index.end()) continue;
        const auto& ra = st[ia->second].rect;
        const auto& rb = st[ib->second].rect;
        const coord_t ca = ra.x + ra.w * 0.5;
        const coord_t cb = rb.x + rb.w * 0.5;
        // Mirror distance: ca should be as far left of axis as cb is right (and vice versa).
        const coord_t da = axis - ca;
        const coord_t db = cb - axis;
        cost += std::abs(da - db);
        // Y alignment is often desired for matched pairs; treat as soft.
        cost += 0.25 * std::abs((ra.y + ra.h * 0.5) - (rb.y + rb.h * 0.5));
    }
    return cost;
}

[[nodiscard]] double total_wirelength_cost(
    const Problem& p,
    const std::vector<InstState>& st,
    const std::unordered_map<std::string, int>& inst_index) {
    double wl = 0.0;
    for (const auto& net : p.nets) {
        wl += static_cast<double>(hpwl_for_net(net, p.instances, st, inst_index));
    }
    return wl;
}

[[nodiscard]] double area_used_cost(
    const std::vector<InstState>& st) {
    coord_t minx = std::numeric_limits<coord_t>::infinity();
    coord_t maxx = -std::numeric_limits<coord_t>::infinity();
    coord_t miny = std::numeric_limits<coord_t>::infinity();
    coord_t maxy = -std::numeric_limits<coord_t>::infinity();
    for (const auto& is : st) {
        minx = std::min(minx, is.rect.x);
        miny = std::min(miny, is.rect.y);
        maxx = std::max(maxx, is.rect.x + is.rect.w);
        maxy = std::max(maxy, is.rect.y + is.rect.h);
    }
    if (!std::isfinite(minx) || !std::isfinite(miny)) return 0.0;
    return static_cast<double>(std::max<coord_t>(0.0, (maxx - minx) * (maxy - miny)));
}

[[nodiscard]] double objective(
    const Problem& p,
    const Options& opt,
    const std::vector<InstState>& st,
    const std::unordered_map<std::string, int>& inst_index) {

    const int ov = overlap_count(p, st);
    const double area = area_used_cost(st);
    const double wl = total_wirelength_cost(p, st, inst_index);
    const double cong = congestion_cost(p, st, inst_index, opt.cong_bins_x, opt.cong_bins_y);
    const double sym = symmetry_imbalance_cost(p, st, inst_index);
    return opt.w_area * area + opt.w_wl * wl + opt.w_cong * cong + opt.w_sym * sym + opt.w_overlap * static_cast<double>(ov);
}

} // namespace

Problem problem_from_json(const json& j) {
    Problem p;
    p.outline.w = j.at("outline").value("w", 0.0);
    p.outline.h = j.at("outline").value("h", 0.0);
    if (p.outline.w <= 0 || p.outline.h <= 0) {
        throw std::runtime_error("outline.w and outline.h must be > 0");
    }

    for (const auto& ji : j.at("instances")) {
        Instance inst;
        inst.id = ji.at("id").get<std::string>();
        inst.device_type = ji.value("device_type", "");
        inst.fixed = ji.value("fixed", false);
        if (inst.fixed) {
            const auto& fr = ji.at("fixed_rect");
            inst.fixed_rect = {fr.value("x", 0.0), fr.value("y", 0.0), fr.value("w", 0.0), fr.value("h", 0.0)};
        }

        for (const auto& jv : ji.at("variants")) {
            Variant v;
            v.name = jv.value("name", std::string{});
            v.w = jv.at("w").get<double>();
            v.h = jv.at("h").get<double>();
            if (v.w <= 0 || v.h <= 0) throw std::runtime_error("Variant w/h must be > 0 for instance " + inst.id);
            if (jv.contains("pins")) {
                for (const auto& jp : jv.at("pins")) {
                    Pin pin;
                    pin.name = jp.at("name").get<std::string>();
                    pin.offset = {jp.value("x", 0.0), jp.value("y", 0.0)};
                    v.pins.push_back(std::move(pin));
                }
            }
            if (jv.contains("allowed_orientations")) {
                for (const auto& jo : jv.at("allowed_orientations")) {
                    v.allowed_orientations.push_back(parse_orient(jo.get<std::string>()));
                }
            } else {
                v.allowed_orientations = {Orientation::R0, Orientation::R180, Orientation::MX, Orientation::MY};
            }
            inst.variants.push_back(std::move(v));
        }
        if (inst.variants.empty()) throw std::runtime_error("Instance " + inst.id + " has no variants");
        p.instances.push_back(std::move(inst));
    }

    if (j.contains("nets")) {
        for (const auto& jn : j.at("nets")) {
            Net n;
            n.name = jn.at("name").get<std::string>();
            n.weight = jn.value("weight", 1.0);
            for (const auto& jp : jn.at("pins")) {
                NetPinRef r;
                r.inst_id = jp.at("inst").get<std::string>();
                r.pin_name = jp.at("pin").get<std::string>();
                n.pins.push_back(std::move(r));
            }
            p.nets.push_back(std::move(n));
        }
    }

    if (j.contains("symmetry")) {
        const auto& s = j.at("symmetry");
        p.symmetry.vertical = s.value("vertical", true);
        if (s.contains("axis")) {
            p.symmetry.has_axis = true;
            p.symmetry.axis = s.at("axis").get<double>();
        }
        if (s.contains("pairs")) {
            for (const auto& pr : s.at("pairs")) {
                SymmetryPair sp;
                sp.a = pr.at("a").get<std::string>();
                sp.b = pr.at("b").get<std::string>();
                p.symmetry.pairs.push_back(std::move(sp));
            }
        }
    }

    return p;
}

Options options_from_json(const json& j) {
    Options o;
    if (!j.contains("options")) return o;
    const auto& opt = j.at("options");
    o.seed = opt.value("seed", o.seed);
    o.iters = opt.value("iters", o.iters);
    o.init_temp = opt.value("init_temp", o.init_temp);
    o.final_temp = opt.value("final_temp", o.final_temp);
    o.w_area = opt.value("w_area", o.w_area);
    o.w_wl = opt.value("w_wl", o.w_wl);
    o.w_cong = opt.value("w_cong", o.w_cong);
    o.w_sym = opt.value("w_sym", o.w_sym);
    o.w_overlap = opt.value("w_overlap", o.w_overlap);
    o.cong_bins_x = opt.value("cong_bins_x", o.cong_bins_x);
    o.cong_bins_y = opt.value("cong_bins_y", o.cong_bins_y);
    return o;
}

Result place(const Problem& p, const Options& opt) {
    const auto t0 = std::chrono::steady_clock::now();

    std::unordered_map<std::string, int> inst_index;
    inst_index.reserve(p.instances.size());
    for (int i = 0; i < static_cast<int>(p.instances.size()); ++i) inst_index[p.instances[i].id] = i;

    std::mt19937 rng(static_cast<std::uint32_t>(opt.seed));
    std::uniform_real_distribution<double> unif01(0.0, 1.0);
    auto rand_coord = [&](coord_t maxv) -> coord_t {
        return static_cast<coord_t>(unif01(rng) * std::max<coord_t>(0.0, maxv));
    };

    std::vector<InstState> st(p.instances.size());
    // Initialize random inside outline (or fixed).
    for (size_t i = 0; i < p.instances.size(); ++i) {
        const auto& inst = p.instances[i];
        auto& is = st[i];
        is.variant_idx = 0;
        const auto& v = inst.variants[0];
        is.orient = v.allowed_orientations.empty() ? Orientation::R0 : v.allowed_orientations[0];
        if (inst.fixed) {
            is.rect = inst.fixed_rect;
            is.variant_idx = 0;
        } else {
            is.rect = clamp_to_outline({rand_coord(p.outline.w - v.w), rand_coord(p.outline.h - v.h), v.w, v.h}, p.outline);
        }
    }
    // If symmetry has an axis, initialize pairs roughly mirrored.
    const coord_t axis = p.symmetry.has_axis ? p.symmetry.axis : (p.outline.w * 0.5);
    for (const auto& sp : p.symmetry.pairs) {
        auto ia = inst_index.find(sp.a);
        auto ib = inst_index.find(sp.b);
        if (ia == inst_index.end() || ib == inst_index.end()) continue;
        auto& a = st[ia->second];
        auto& b = st[ib->second];
        if (p.instances[ia->second].fixed || p.instances[ib->second].fixed) continue;
        const coord_t ca = a.rect.x + a.rect.w * 0.5;
        const coord_t da = axis - ca;
        const coord_t cb = axis + da;
        b.rect.x = std::clamp(cb - b.rect.w * 0.5, 0.0, std::max<coord_t>(0.0, p.outline.w - b.rect.w));
        b.rect.y = a.rect.y;
    }

    double best_obj = objective(p, opt, st, inst_index);
    auto best = st;

    auto temperature = [&](int k) -> double {
        if (opt.iters <= 1) return opt.final_temp;
        const double t = static_cast<double>(k) / static_cast<double>(opt.iters - 1);
        // Exponential schedule.
        return opt.init_temp * std::pow(opt.final_temp / opt.init_temp, t);
    };

    std::uniform_int_distribution<int> pick_inst(0, static_cast<int>(p.instances.size()) - 1);

    for (int iter = 0; iter < opt.iters; ++iter) {
        const double T = temperature(iter);

        const int i = pick_inst(rng);
        if (p.instances[i].fixed) continue;

        auto cand = st;
        auto& is = cand[i];
        const auto& inst = p.instances[i];

        const double r = unif01(rng);
        if (r < 0.55) {
            // Move.
            const coord_t step_x = p.outline.w * 0.05;
            const coord_t step_y = p.outline.h * 0.05;
            std::normal_distribution<double> n01(0.0, 1.0);
            is.rect.x += static_cast<coord_t>(n01(rng) * step_x);
            is.rect.y += static_cast<coord_t>(n01(rng) * step_y);
            is.rect = clamp_to_outline(is.rect, p.outline);
        } else if (r < 0.85) {
            // Change variant.
            std::uniform_int_distribution<int> pick_var(0, static_cast<int>(inst.variants.size()) - 1);
            is.variant_idx = pick_var(rng);
            const auto& v = inst.variants[is.variant_idx];
            is.rect.w = v.w;
            is.rect.h = v.h;
            is.rect = clamp_to_outline(is.rect, p.outline);
        } else {
            // Change orientation.
            const auto& v = inst.variants[is.variant_idx];
            if (!v.allowed_orientations.empty()) {
                std::uniform_int_distribution<int> pick_o(0, static_cast<int>(v.allowed_orientations.size()) - 1);
                is.orient = v.allowed_orientations[pick_o(rng)];
            }
        }

        // Enforce hard-ish symmetry move coupling: if i is in a pair, update its mate by mirroring X and copying Y.
        for (const auto& sp : p.symmetry.pairs) {
            if (sp.a == inst.id || sp.b == inst.id) {
                const std::string mate_id = (sp.a == inst.id) ? sp.b : sp.a;
                auto im = inst_index.find(mate_id);
                if (im == inst_index.end()) break;
                if (p.instances[im->second].fixed) break;
                auto& m = cand[im->second];
                const coord_t ci = cand[i].rect.x + cand[i].rect.w * 0.5;
                const coord_t di = axis - ci;
                const coord_t cm = axis + di;
                m.rect.x = std::clamp(cm - m.rect.w * 0.5, 0.0, std::max<coord_t>(0.0, p.outline.w - m.rect.w));
                m.rect.y = cand[i].rect.y;
                break;
            }
        }

        const double cur = objective(p, opt, st, inst_index);
        const double nxt = objective(p, opt, cand, inst_index);
        const double d = nxt - cur;

        const bool accept = (d <= 0.0) || (std::exp(-d / std::max(1e-9, T)) > unif01(rng));
        if (accept) st.swap(cand);

        const double cur2 = accept ? nxt : cur;
        if (cur2 < best_obj) {
            best_obj = cur2;
            best = st;
        }
    }

    Result r;
    r.placed.reserve(best.size());
    for (size_t i = 0; i < best.size(); ++i) {
        PlacedInstance pi;
        pi.id = p.instances[i].id;
        pi.variant_idx = best[i].variant_idx;
        pi.orient = best[i].orient;
        pi.rect = best[i].rect;
        r.placed.push_back(std::move(pi));
    }

    r.metrics.overlaps = overlap_count(p, best);
    r.metrics.area_used = area_used_cost(best);
    r.metrics.wl_est = total_wirelength_cost(p, best, inst_index);
    r.metrics.congestion_est = congestion_cost(p, best, inst_index, opt.cong_bins_x, opt.cong_bins_y);
    r.metrics.symmetry_imbalance = symmetry_imbalance_cost(p, best, inst_index);
    const auto t1 = std::chrono::steady_clock::now();
    r.metrics.runtime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    return r;
}

json result_to_json(const Result& r) {
    json out;
    out["placed"] = json::array();
    for (const auto& p : r.placed) {
        json j;
        j["id"] = p.id;
        j["x"] = p.rect.x;
        j["y"] = p.rect.y;
        j["w"] = p.rect.w;
        j["h"] = p.rect.h;
        j["variant_idx"] = p.variant_idx;
        j["orient"] = orient_to_string(p.orient);
        out["placed"].push_back(std::move(j));
    }
    out["metrics"] = {
        {"area_used", r.metrics.area_used},
        {"wl_est", r.metrics.wl_est},
        {"congestion_est", r.metrics.congestion_est},
        {"symmetry_imbalance", r.metrics.symmetry_imbalance},
        {"overlaps", r.metrics.overlaps},
        {"runtime_ms", r.metrics.runtime_ms},
    };
    return out;
}

} // namespace eda_placer::analog

