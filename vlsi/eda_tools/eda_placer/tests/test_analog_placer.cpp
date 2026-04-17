#include "eda_placer/analog_placer.hpp"

#include <cassert>
#include <iostream>

using json = nlohmann::json;

int main() {
    // Minimal synthetic analog test: two mirrored devices and one net.
    const json prob = {
        {"outline", {{"w", 100.0}, {"h", 60.0}}},
        {"instances", json::array({
            {
                {"id", "M1"},
                {"device_type", "nmos"},
                {"variants", json::array({
                    {{"name","v0"},{"w",10.0},{"h",6.0},{"pins", json::array({{{"name","G"},{"x",5.0},{"y",3.0}}})}}
                })}
            },
            {
                {"id", "M2"},
                {"device_type", "nmos"},
                {"variants", json::array({
                    {{"name","v0"},{"w",10.0},{"h",6.0},{"pins", json::array({{{"name","G"},{"x",5.0},{"y",3.0}}})}}
                })}
            }
        })},
        {"nets", json::array({
            {{"name","VIN"},{"weight",2.0},{"pins", json::array({{{"inst","M1"},{"pin","G"}},{{"inst","M2"},{"pin","G"}}})}}
        })},
        {"symmetry", {{"vertical", true}, {"axis", 50.0}, {"pairs", json::array({{{"a","M1"},{"b","M2"}}})}}}
    };

    const auto p = eda_placer::analog::problem_from_json(prob);
    eda_placer::analog::Options opt;
    opt.seed = 7;
    opt.iters = 3000;
    opt.w_overlap = 1e6;

    const auto r = eda_placer::analog::place(p, opt);
    const auto out = eda_placer::analog::result_to_json(r);

    assert(out.contains("placed"));
    assert(out.contains("metrics"));
    assert(static_cast<int>(out.at("placed").size()) == 2);
    // Expect low overlap count for such small problem.
    const int overlaps = out.at("metrics").value("overlaps", 999);
    if (overlaps != 0) {
        std::cerr << "Unexpected overlaps=" << overlaps << "\n";
        return 1;
    }
    std::cout << "ok\n";
    return 0;
}

