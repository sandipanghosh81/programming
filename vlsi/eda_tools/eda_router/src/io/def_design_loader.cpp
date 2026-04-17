// ═══════════════════════════════════════════════════════════════════════════════
// DEF physical subset parser — implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "routing_genetic_astar/io/def_design_loader.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace routing_genetic_astar {
namespace {

[[nodiscard]] std::string trim(std::string_view s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

[[nodiscard]] bool starts_with(std::string_view s, std::string_view p) {
    return s.size() >= p.size() && s.substr(0, p.size()) == p;
}

[[nodiscard]] std::string read_entire_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

[[nodiscard]] std::string strip_hash_comments(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '#') {
            while (i < raw.size() && raw[i] != '\n') ++i;
            if (i < raw.size()) out.push_back('\n');
            continue;
        }
        out.push_back(raw[i]);
    }
    return out;
}

[[nodiscard]] bool parse_diearea(const std::string& line,
                               std::int64_t& x1, std::int64_t& y1,
                               std::int64_t& x2, std::int64_t& y2) {
    auto pos = line.find("DIEAREA");
    if (pos == std::string::npos) return false;
    std::istringstream iss(line.substr(pos));
    std::string kw, lp1, rp1, lp2, rp2;
    iss >> kw >> lp1;
    if (lp1 != "(") return false;
    std::int64_t a, b, c, d;
    if (!(iss >> a >> b)) return false;
    iss >> rp1 >> lp2;
    if (rp1 != ")" || lp2 != "(") return false;
    if (!(iss >> c >> d)) return false;
    x1 = a;
    y1 = b;
    x2 = c;
    y2 = d;
    return true;
}

[[nodiscard]] bool parse_units_microns(const std::string& line, int& dbu_per_micron) {
    if (line.find("UNITS") == std::string::npos) return false;
    if (line.find("MICRONS") == std::string::npos) return false;
    std::istringstream iss(line);
    std::string u, d, m;
    int val = 0;
    if (!(iss >> u >> d >> m >> val)) return false;
    dbu_per_micron = val;
    return true;
}

[[nodiscard]] bool parse_design_name(const std::string& line, std::string& name) {
    std::istringstream iss(line);
    std::string d;
    name.clear();
    if (!(iss >> d >> name)) return false;
    return d == "DESIGN";
}

[[nodiscard]] bool parse_component_line(const std::string& line, CellPlacement& c) {
    std::istringstream iss(line);
    std::string dash, inst, ref, plus, placed, lp;
    if (!(iss >> dash >> inst >> ref >> plus >> placed >> lp)) return false;
    if (dash != "-" || plus != "+" || placed != "PLACED" || lp != "(") return false;
    std::int64_t x = 0, y = 0;
    std::string rp, orient;
    if (!(iss >> x >> y >> rp >> orient)) return false;
    if (rp != ")") return false;
    c.inst_name = inst;
    c.ref_name  = ref;
    c.x_dbu     = x;
    c.y_dbu     = y;
    return true;
}

[[nodiscard]] int count_pin_tuples(std::string_view block) {
    int n = 0;
    for (size_t i = 0; i < block.size(); ++i)
        if (block[i] == '(') ++n;
    return std::max(2, n); // at least 2 pins per routed net
}

} // namespace

std::expected<ParsedDefDesign, DefParseError> parse_def_file(const std::string& path) {
    ParsedDefDesign out;
    const std::string raw = read_entire_file(path);
    if (raw.empty())
        return std::unexpected(DefParseError{"Cannot read DEF file or empty: " + path});

    const std::string text = strip_hash_comments(raw);
    std::vector<std::string> lines;
    {
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line)) {
            line = trim(line);
            if (!line.empty()) lines.push_back(line);
        }
    }

    enum class Section { Top, Components, Nets };
    Section sec = Section::Top;

    for (size_t li = 0; li < lines.size(); ++li) {
        const std::string& line = lines[li];

        if (starts_with(line, "DESIGN ")) {
            std::string dn;
            if (parse_design_name(line, dn)) out.design_name = std::move(dn);
            continue;
        }
        if (parse_units_microns(line, out.dbu_per_micron)) continue;
        if (parse_diearea(line, out.die_xlo, out.die_ylo, out.die_xhi, out.die_yhi)) continue;

        if (line == "END COMPONENTS" || starts_with(line, "END COMPONENTS")) {
            sec = Section::Top;
            continue;
        }
        if (line == "END NETS" || starts_with(line, "END NETS")) {
            sec = Section::Top;
            continue;
        }
        if (starts_with(line, "COMPONENTS ")) {
            sec = Section::Components;
            continue;
        }
        if (starts_with(line, "NETS ")) {
            sec = Section::Nets;
            continue;
        }

        if (sec == Section::Components && starts_with(line, "- ")) {
            CellPlacement c;
            if (parse_component_line(line, c)) out.cells.push_back(std::move(c));
            continue;
        }

        if (sec == Section::Nets && starts_with(line, "- ")) {
            std::istringstream ns(line.substr(2));
            NetDefinition net;
            ns >> net.name;
            net.id = static_cast<int>(out.summary.nets.size());

            std::string block;
            std::string remainder;
            std::getline(ns, remainder); // pins on same line as net name
            block += remainder;

            size_t j = li + 1;
            for (; j < lines.size(); ++j) {
                const std::string& L = lines[j];
                if (L == ";" || (L.size() == 1 && L[0] == ';')) break;
                block += ' ';
                block += L;
            }
            li = j;

            const int pins = count_pin_tuples(block);
            net.pins.resize(static_cast<size_t>(pins), GridPoint{0, 0, 0});
            out.summary.nets.push_back(std::move(net));
            continue;
        }
    }

    if (out.die_xhi <= out.die_xlo && !out.cells.empty()) {
        std::int64_t minx = out.cells[0].x_dbu, maxx = out.cells[0].x_dbu;
        std::int64_t miny = out.cells[0].y_dbu, maxy = out.cells[0].y_dbu;
        for (const auto& c : out.cells) {
            minx = std::min(minx, c.x_dbu);
            maxx = std::max(maxx, c.x_dbu);
            miny = std::min(miny, c.y_dbu);
            maxy = std::max(maxy, c.y_dbu);
        }
        const std::int64_t margin = 2000;
        out.die_xlo = minx - margin;
        out.die_ylo = miny - margin;
        out.die_xhi = maxx + margin;
        out.die_yhi = maxy + margin;
    }
    if (out.die_xhi <= out.die_xlo) {
        out.die_xlo = 0;
        out.die_ylo = 0;
        out.die_xhi = 100000;
        out.die_yhi = 100000;
    }

    out.summary.total_nets = static_cast<int>(out.summary.nets.size());
    out.summary.total_pins = 0;
    for (const auto& n : out.summary.nets) out.summary.total_pins += static_cast<int>(n.pins.size());
    out.summary.avg_fanout =
        out.summary.total_nets > 0
            ? static_cast<float>(out.summary.total_pins) / static_cast<float>(out.summary.total_nets)
            : 0.0f;
    out.summary.max_fanout = 0;
    for (const auto& n : out.summary.nets)
        out.summary.max_fanout = std::max(out.summary.max_fanout, static_cast<float>(n.pins.size()));

    return out;
}

void snap_to_grid(ParsedDefDesign& design, int rows, int cols) {
    const std::int64_t w = std::max<std::int64_t>(1, design.die_xhi - design.die_xlo);
    const std::int64_t h = std::max<std::int64_t>(1, design.die_yhi - design.die_ylo);

    auto map_x = [&](std::int64_t x_dbu) -> int {
        const double t = static_cast<double>(x_dbu - design.die_xlo) / static_cast<double>(w);
        int gx = static_cast<int>(t * static_cast<double>(cols - 1));
        return std::clamp(gx, 0, cols - 1);
    };
    auto map_y = [&](std::int64_t y_dbu) -> int {
        const double t = static_cast<double>(y_dbu - design.die_ylo) / static_cast<double>(h);
        int gy = static_cast<int>(t * static_cast<double>(rows - 1));
        return std::clamp(gy, 0, rows - 1);
    };

    for (auto& c : design.cells) {
        c.grid_x = map_x(c.x_dbu);
        c.grid_y = map_y(c.y_dbu);
    }

    int k = 0;
    for (auto& net : design.summary.nets) {
        for (size_t i = 0; i < net.pins.size(); ++i) {
            const int off = static_cast<int>((k + i) % std::max(1, cols - 2));
            net.pins[i].x = 1 + (off % std::max(1, cols - 2));
            net.pins[i].y = 1 + (off / std::max(1, cols / 4)) % std::max(1, rows - 2);
            net.pins[i].z = static_cast<int>(i % std::max(1, 1)); // layer 0 default
        }
        ++k;
    }
}

} // namespace routing_genetic_astar
