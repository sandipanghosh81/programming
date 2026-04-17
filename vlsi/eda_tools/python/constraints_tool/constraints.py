import re
import math
import json
import argparse
import contextlib
import io
import os
import time
from collections import defaultdict
from dataclasses import dataclass
from typing import List, Dict, Set, Tuple
from pathlib import Path
from pprint import pprint

# Plotting is optional; keep constraint extraction usable in minimal environments
# (e.g., when driving placement via vlsi_agent without GUI dependencies).
try:
    from matplotlib.figure import Figure  # type: ignore
    from matplotlib import colors as mcolors  # type: ignore
    import matplotlib.pyplot as plt  # type: ignore
except Exception:  # pragma: no cover
    Figure = object  # type: ignore
    mcolors = None  # type: ignore
    plt = None  # type: ignore
# OR-Tools is optional; keep core constraint extraction usable without it.
try:
    from modgen_scaling_rmst import (  # type: ignore
        solve_universal_expansion,
        connectivity_rules_from_rows,
        pre_check_template,
    )
except Exception:  # pragma: no cover
    def solve_universal_expansion(*args, **kwargs):  # type: ignore
        raise RuntimeError(
            "modgen_scaling_rmst unavailable (missing optional dependency like ortools). "
            "Install ortools or run without template expansion."
        )

    def connectivity_rules_from_rows(rows):  # type: ignore
        return None

    def pre_check_template(rows, rules=None):  # type: ignore
        return True, "skipped (optional dependency missing)"


# ==========================================
# 1. Core Data Structures
# ==========================================

@dataclass(eq=False)
class Device:
    name: str
    type: str  # 'nmos' or 'pmos'
    d: str  # Drain Net
    g: str  # Gate Net
    s: str  # Source Net
    b: str = ""  # Body/Bulk Net (optional)
    w: float = 0.0
    l: float = 0.0
    mult: int = 1  # Multiplicity (m/mult/nf)
    model: str = ""

    def __repr__(self):
        return f"{self.name}({self.type}, W={self.w}, L={self.l}, M={self.mult})"


class Circuit:
    def __init__(self):
        self.devices: List[Device] = []
        # Net -> List of Devices connected to it
        self.net_map: Dict[str, List[Device]] = defaultdict(list)

    def add_device(self, device: Device):
        self.devices.append(device)
        # Index connections for O(1) lookups
        for net in [device.d, device.g, device.s, device.b]:
            if not net:
                continue
            self.net_map[net].append(device)


def _load_pptx_notes() -> Dict[str, str]:
    notes_path = Path(__file__).parent / "constraints_pptx_text.txt"
    if not notes_path.exists():
        return {}
    text = notes_path.read_text(encoding="utf-8")
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    keywords = {
        "diode_connected": "diode-connected",
        "inverter": "cmos inverter",
        "transmission_gate": "transmission gate",
        "diff_pair": "differential pair",
        "cross_coupled": "cross-coupled",
        "current_mirror": "current mirror",
        "cascode": "cascode",
        "source_follower": "source follower",
        "folded_cascode": "folded cascode",
        "active_load": "active load",
        "two_stage_amp": "two-stage amplifier",
    }
    notes: Dict[str, str] = {}
    for key, needle in keywords.items():
        hits = [ln for ln in lines if needle in ln.lower()]
        if hits:
            notes[key] = " ".join(hits)
    return notes


_PPTX_NOTES = _load_pptx_notes()


STRUCTURE_DESCRIPTIONS: Dict[str, Dict[str, str]] = {
    "diode_connected": {
        "web": (
            "Gate tied to drain forces VGS=VDS, biasing the MOSFET in saturation. "
            "Used as a VGS reference in current mirrors and bias generators."
        ),
        "code": "Detect dev.d == dev.g; report as diode-connected reference device.",
    },
    "inverter": {
        "web": (
            "Complementary NMOS/PMOS pair with gates tied (input) and drains tied (output) "
            "provides rail-to-rail inversion."
        ),
        "code": "Find NMOS/PMOS sharing gate and drain nets; report as CMOS inverter.",
    },
    "transmission_gate": {
        "web": (
            "Parallel NMOS/PMOS pass devices between the same two nodes with complementary controls; "
            "passes both logic highs and lows with low resistance."
        ),
        "code": "Find NMOS/PMOS sharing D/S nodes (allowing swap) to form bidirectional pass gate.",
    },
    "diff_pair": {
        "web": (
            "Matched pair with a common tail node that amplifies the differential input while rejecting "
            "common-mode signals."
        ),
        "code": "Same type and W/L, shared source (tail), different gates/drains on signal nets.",
    },
    "cross_coupled": {
        "web": (
            "Two transistors with each gate connected to the other’s drain, forming a bistable latch or "
            "positive feedback pair."
        ),
        "code": "Detect mutual gate-drain cross connection between two like-type devices.",
    },
    "current_mirror": {
        "web": (
            "Devices share gate and source; a diode-connected reference sets VGS so the output device "
            "mirrors the reference current."
        ),
        "code": "Same type, shared gate/source, at least one diode-connected device in the pair.",
    },
    "cascode": {
        "web": (
            "Stacked transistors keep the lower device’s drain nearly constant, boosting output "
            "resistance and gain."
        ),
        "code": "Same-type stack with top source tied to bottom drain.",
    },
    "source_follower": {
        "web": (
            "Common-drain stage where the gate is the input and the source is the output; provides "
            "near-unity gain buffer with low output impedance."
        ),
        "code": "Detect drain on supply and gate/source on signal nets to identify follower.",
    },
    "folded_cascode": {
        "web": (
            "Mixed-type cascode where signal current is ‘folded’ into the opposite-type branch to "
            "increase swing while preserving gain."
        ),
        "code": "Detect stacked devices of opposite types sharing drain/source nodes.",
    },
    "active_load": {
        "web": (
            "Current-mirror load used with a differential pair to convert differential currents to a "
            "single-ended output and improve gain."
        ),
        "code": "Match mirror drain nodes to diff-pair drains with a diode-connected reference.",
    },
    "two_stage_amp": {
        "web": (
            "Differential input stage followed by a second gain stage (common-source/emitter) for high "
            "overall gain; typically Miller-compensated."
        ),
        "code": "Diff-pair output drives the gate of a second-stage device with a distinct output net.",
    },
}


def get_structure_description(structure: str) -> str:
    meta = STRUCTURE_DESCRIPTIONS.get(structure, {})
    pptx_note = _PPTX_NOTES.get(structure, "")
    parts = [
        f"Web: {meta.get('web', 'N/A')}",
        f"PPTX: {pptx_note or 'N/A'}",
        f"Code: {meta.get('code', 'N/A')}",
    ]
    return " | ".join(parts)


class Design:
    def __init__(self, circuit: Circuit):
        self.circuit = circuit
        self.schematic_data: Dict[str, object] = {}
        self.layout_data: Dict[str, object] = {}

    def distill_schematic_data(self) -> Dict[str, object]:
        instances: Dict[str, Dict[str, object]] = {}
        nets: Dict[str, List[Dict[str, str]]] = defaultdict(list)

        for dev in self.circuit.devices:
            terminals = {"d": dev.d, "g": dev.g, "s": dev.s, "b": dev.b}
            instances[dev.name] = {
                "type": dev.type,
                "multiplicity": dev.mult,
                "terminals": terminals,
                "w": dev.w,
                "l": dev.l,
                "model": dev.model,
            }
            for term, net in terminals.items():
                if not net:
                    continue
                nets[net].append({"instance": dev.name, "type": dev.type, "terminal": term})

        structures = collect_structure_instances(self.circuit)
        self.schematic_data = {
            "instances": instances,
            "nets": dict(nets),
            "structures": structures,
        }
        return self.schematic_data

    def distill_layout_data(self) -> Dict[str, object]:
        specs = map_layout_requirements(self.circuit)
        floorplan = create_floorplan_guard_rings(self.circuit, specs)
        net_constraints = create_net_requirement_map(self.circuit, specs)
        grid_plan = create_guard_ring_grid_floorplan(floorplan, self.circuit)
        self.layout_data = {
            "layout_specs": specs,
            "guard_rings": floorplan,
            "net_constraints": net_constraints,
            "grid_plan": grid_plan,
        }
        return self.layout_data

    def run_layout_tests(self) -> bool:
        try:
            self._run_symmetry_dummy_tests()
            grid_plan = self.layout_data.get("grid_plan", {})
            for guard_id, data in grid_plan.items():
                modgen_rows = data.get("modgen_rows")
                if not modgen_rows:
                    continue
                rules = data.get("connectivity_rules") or connectivity_rules_from_rows(modgen_rows)
                ok, msg = pre_check_template(modgen_rows, rules)
                if not ok:
                    raise AssertionError(f"{guard_id}: connectivity test failed - {msg}")
            return True
        except AssertionError as exc:
            print("\n--- Layout Tests Failed ---")
            print(f"  {exc}")
            self._dump_edge_render_state()
            return False

    def export_data(self) -> Dict[str, object]:
        if not self.schematic_data:
            self.distill_schematic_data()
        if not self.layout_data:
            self.distill_layout_data()
        return {
            "schematic_data": self.schematic_data,
            "layout_data": self.layout_data,
        }

    def qualityTest(self) -> Dict[str, object]:
        if not self.layout_data:
            self.distill_layout_data()
        floorplan = self.layout_data.get("guard_rings", {})
        grid_plan = self.layout_data.get("grid_plan", {})
        report: Dict[str, object] = {
            "guard_rings": {},
            "summary": {"total_checks": 0, "failed_checks": 0},
        }

        def _adjacent_pairs(rows: List[List[str]]) -> Set[Tuple[str, str]]:
            pairs: Set[Tuple[str, str]] = set()
            for row in rows:
                for a, b in zip(row, row[1:]):
                    if a == "X" or b == "X":
                        continue
                    pairs.add((a, b))
                    pairs.add((b, a))
            return pairs

        count_equal_keys = {
            "symmetry_needed",
            "common_centroid_needed",
            "interdigitation_needed",
            "matchlength_routing_needed",
            "matched_device_dimensions_needed",
            "matched_orientation_needed",
        }
        mirror_keys = {"symmetry_needed", "common_centroid_needed"}
        adjacency_keys = {"interdigitation_needed", "abutment_needed"}

        for guard_id, data in floorplan.items():
            if guard_id in {"_device_types", "cross_gr_match"}:
                continue
            guard_report: Dict[str, object] = {"requirements": []}
            grid = grid_plan.get(guard_id)
            if not grid:
                guard_report["requirements"].append({
                    "requirement": "grid_plan",
                    "group": [],
                    "status": "fail",
                    "issues": ["missing grid_plan for guard ring"],
                })
                report["guard_rings"][guard_id] = guard_report
                report["summary"]["total_checks"] += 1
                report["summary"]["failed_checks"] += 1
                continue

            placement = grid.get("placement", {})
            cols = int(grid.get("cols", 0))
            modgen_rows = grid.get("modgen_rows", [])
            adj_pairs = _adjacent_pairs(modgen_rows) if modgen_rows else set()
            positions_by_source: Dict[str, List[Tuple[int, int]]] = defaultdict(list)
            for name, pos in placement.items():
                if pos.get("virtual"):
                    continue
                source = str(pos.get("source") or name)
                positions_by_source[source].append((int(pos.get("row", 0)), int(pos.get("col", 0))))

            for req_key, groups in data.items():
                if req_key in {"grtype", "devtype"}:
                    continue
                for group in groups:
                    group_sources = [str(name).split("(")[0] for name in group]
                    if not group_sources:
                        continue
                    issues: List[str] = []
                    counts = {s: len(positions_by_source.get(s, [])) for s in group_sources}
                    missing = [s for s, c in counts.items() if c == 0]
                    if missing:
                        issues.append(f"missing: {missing}")
                    if req_key in count_equal_keys:
                        if len(set(counts.values())) > 1:
                            issues.append(f"count mismatch: {counts}")
                    if req_key in mirror_keys and len(group_sources) == 2 and cols:
                        a, b = group_sources
                        a_cols = [c for _, c in positions_by_source.get(a, [])]
                        b_cols = [c for _, c in positions_by_source.get(b, [])]
                        mirror_ok = True
                        for col in a_cols:
                            mirror_col = cols - 1 - col
                            if mirror_col not in b_cols:
                                mirror_ok = False
                                break
                        if not mirror_ok:
                            issues.append(f"mirror mismatch: {a} cols {a_cols} vs {b} cols {b_cols}")
                    if req_key in adjacency_keys and adj_pairs:
                        found = False
                        for i in range(len(group_sources)):
                            for j in range(i + 1, len(group_sources)):
                                if (group_sources[i], group_sources[j]) in adj_pairs:
                                    found = True
                                    break
                            if found:
                                break
                        if not found:
                            issues.append("no adjacency between group members")

                    report["summary"]["total_checks"] += 1
                    if issues:
                        report["summary"]["failed_checks"] += 1
                        status = "fail"
                    else:
                        status = "pass"
                    guard_report["requirements"].append({
                        "requirement": req_key,
                        "group": group_sources,
                        "status": status,
                        "issues": issues,
                    })
            report["guard_rings"][guard_id] = guard_report

        return report

    def print_layout_spec(self) -> None:
        specs = self.layout_data.get("layout_specs", [])
        print(f"\n--- Layout Placement/Routing Specification ---")
        if not specs:
            print("  None found.")
            return
        for spec in specs:
            print(f"  Structure: {spec['structure']}")
            print(f"    Devices: {', '.join(spec.get('devices', []))}")
            print(f"    Nets: {spec.get('nets', {})}")
            print(f"    Requirements: {spec.get('layout_requirements', {})}")

    def print_schematic_data(self) -> None:
        data = self.schematic_data
        print("\n--- Schematic Data ---")
        if not data:
            print("  None found.")
            return
        pprint(data, sort_dicts=False)

    def print_floorplan_guard_rings(self) -> None:
        floorplan = self.layout_data.get("guard_rings", {})
        print(f"\n--- Guard Ring Floorplan ---")
        if not floorplan:
            print("  None found.")
            return
        device_gr_types: Dict[str, Set[str]] = defaultdict(set)
        device_types = floorplan.get("_device_types", {})
        for guard_id, data in floorplan.items():
            if guard_id in {"_device_types", "cross_gr_match"}:
                continue
            gr_type = str(data.get("grtype", ""))
            for req_key, groups in data.items():
                if req_key in {"grtype", "devtype"}:
                    continue
                for group in groups:
                    for name in group:
                        device_gr_types[name].add(gr_type)
        for guard_id, data in floorplan.items():
            if guard_id in {"_device_types", "cross_gr_match"}:
                continue
            print(f"  Guard Ring: {guard_id}")
            print(f"    Type: {data.get('grtype')}")
            print(f"    Device Type: {data.get('devtype')}")
            current_gr_type = str(data.get("grtype", ""))
            current_devtype = str(data.get("devtype", ""))
            for req_key, groups in data.items():
                if req_key in {"grtype", "devtype"}:
                    continue
                marked_groups = []
                for group in groups:
                    marked_group = []
                    for name in group:
                        gr_types = device_gr_types.get(name, set())
                        dev_type = device_types.get(name, current_devtype)
                        label = f"{name}({dev_type})"
                        if dev_type != current_devtype and len(gr_types) > 1:
                            marked_group.append(f"*{label}")
                        else:
                            marked_group.append(label)
                    marked_groups.append(marked_group)
                print(f"    {req_key}: {marked_groups}")

        cross_gr = floorplan.get("cross_gr_match", [])
        if cross_gr:
            print("  Cross-Guard-Ring Matches:")
            for group in cross_gr:
                labeled = [f"{name}({device_types.get(name, '')})" for name in group]
                print(f"    {labeled}")

    def print_net_requirement_map(self) -> None:
        net_map = self.layout_data.get("net_constraints", {})
        print(f"\n--- Net Requirement Map ---")
        if not net_map:
            print("  None found.")
            return
        for key, payload in net_map.items():
            print(f"  {key}: {payload}")

    def print_guard_ring_grid_floorplan(self) -> None:
        grid_plan = self.layout_data.get("grid_plan", {})
        print(f"\n--- Guard Ring Grid Floorplan ---")
        if not grid_plan:
            print("  None found.")
            return

        device_map = {dev.name: dev for dev in self.circuit.devices}

        def _short_name(value: str) -> str:
            return str(value or "")

        def _compose_middle(left: str, center: str, right: str, width: int) -> str:
            line = [" "] * width
            for i, ch in enumerate(left[:width]):
                line[i] = ch
            for i, ch in enumerate(right[::-1][:width]):
                idx = width - 1 - i
                line[idx] = ch
            start = max(0, (width - len(center)) // 2)
            for i, ch in enumerate(center[:width]):
                idx = start + i
                if 0 <= idx < width:
                    line[idx] = ch
            return "".join(line)

        for guard_id, data in grid_plan.items():
            rows = int(data.get("rows", 0))
            cols = int(data.get("cols", 0))
            placement = data.get("placement", {})
            print(f"  Guard Ring: {guard_id}")
            print(f"    Type: {data.get('grtype')}")
            print(f"    Device Type: {data.get('devtype')}")
            print(f"    Grid: {rows} x {cols}")
            if rows == 0 or cols == 0:
                continue

            render_rows = _build_render_rows(
                rows,
                cols,
                placement,
                device_map,
                edge_label=False,
                enforce_even_cols=True,
            )
            max_cols = max((len(row) for row in render_rows), default=0)

            line_candidates: List[str] = []
            for row in render_rows:
                for cell in row:
                    if cell.get("virtual"):
                        line_candidates.append("X")
                        continue
                    dev = cell.get("device")
                    if dev is None:
                        line_candidates.append("X")
                        continue
                    g_line = f"g:{_short_name(dev.g or '-')}"
                    b_line = f"b:{_short_name(dev.b or '-')}"
                    label = f"{_short_name(cell.get('name'))}({_short_name(cell.get('type'))})"
                    left = f"{cell.get('left_label')}:{_short_name(cell.get('left_net'))}"
                    right = f"{cell.get('right_label')}:{_short_name(cell.get('right_net'))}"
                    line_candidates.extend([g_line, b_line, f"{left} {label} {right}"])

            max_len = max(6, max((len(s) for s in line_candidates), default=0))
            cell_w = max_len + 2

            def _hline(left: str, mid: str, right: str) -> str:
                return left + mid.join(["-" * cell_w for _ in range(max_cols + 1)]) + right

            header_cells = ["".center(cell_w)] + [str(c).center(cell_w) for c in range(max_cols)]
            print(_hline("+", "+", "+"))
            print("|" + "|".join(header_cells) + "|")
            print(_hline("+", "+", "+"))
            for r, row in enumerate(render_rows):
                row_body_net = "-"
                for cell in row:
                    dev = cell.get("device")
                    if dev is not None:
                        row_body_net = dev.b or dev.s or "-"
                        break
                top_cells = ["".center(cell_w)]
                mid_cells = [str(r).center(cell_w)]
                bot_cells = ["".center(cell_w)]
                for cell in row:
                    if cell.get("virtual"):
                        g_line = f"g:{_short_name(row_body_net)}".center(cell_w)
                        b_line = f"b:{_short_name(row_body_net)}".center(cell_w)
                        left_label = cell.get("left_label") or "s"
                        right_label = cell.get("right_label") or "d"
                        left_net = cell.get("left_net") or row_body_net
                        right_net = cell.get("right_net") or row_body_net
                        left = f"{left_label}:{_short_name(left_net)}"
                        right = f"{right_label}:{_short_name(right_net)}"
                        middle = _compose_middle(left, "X", right, cell_w)
                        top_cells.append(g_line)
                        mid_cells.append(middle)
                        bot_cells.append(b_line)
                        continue
                    dev = cell.get("device")
                    if dev is None:
                        g_line = f"g:{_short_name(row_body_net)}".center(cell_w)
                        b_line = f"b:{_short_name(row_body_net)}".center(cell_w)
                        left = f"s:{_short_name(row_body_net)}"
                        right = f"d:{_short_name(row_body_net)}"
                        middle = _compose_middle(left, "X", right, cell_w)
                        top_cells.append(g_line)
                        mid_cells.append(middle)
                        bot_cells.append(b_line)
                        continue
                    g_line = f"g:{_short_name(dev.g or '-')}".center(cell_w)
                    b_line = f"b:{_short_name(dev.b or '-')}".center(cell_w)
                    label = f"{_short_name(cell.get('name'))}({_short_name(cell.get('type'))})"
                    left = f"{cell.get('left_label')}:{_short_name(cell.get('left_net'))}"
                    right = f"{cell.get('right_label')}:{_short_name(cell.get('right_net'))}"
                    middle = _compose_middle(left, label, right, cell_w)
                    top_cells.append(g_line)
                    mid_cells.append(middle)
                    bot_cells.append(b_line)
                print("|" + "|".join(top_cells) + "|")
                print("|" + "|".join(mid_cells) + "|")
                print("|" + "|".join(bot_cells) + "|")
                print(_hline("+", "+", "+"))

    def print_layout_data(self) -> None:
        print("\n--- Layout Data ---")
        if not self.layout_data:
            print("  None found.")
            return
        pprint(self.layout_data, sort_dicts=False)

    def print_design_data(self) -> None:
        if not self.schematic_data:
            self.distill_schematic_data()
        if not self.layout_data:
            self.distill_layout_data()
        self.print_schematic_data()
        self.print_layout_data()

    def _build_preview_lines(self, lines: List[str]) -> None:
        for line in lines:
            print(line)

    def _write_preview_lines(self, lines: List[str], out_path: Path | None) -> None:
        if not out_path:
            return
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")

    def _build_guard_ring_color_map(
        self,
        guard_id: str,
        placement: Dict[str, Dict[str, object]],
        floorplan: Dict[str, Dict[str, object]],
    ) -> Tuple[Dict[str, str], str, Set[str], str]:
        def _color_pool(count: int) -> List[str]:
            base = [
                "#1f77b4",
                "#ff7f0e",
                "#2ca02c",
                "#d62728",
                "#9467bd",
                "#8c564b",
                "#e377c2",
                "#7f7f7f",
                "#bcbd22",
                "#17becf",
            ]
            if count <= len(base):
                return base[:count]
            cmap = plt.get_cmap("tab20")
            extra = [mcolors.to_hex(cmap(i / max(count - 1, 1))) for i in range(count)]
            return base + [c for c in extra if c not in base]

        background_color = "#f8f8f8"
        background_set = {"#f8f8f8", "#fff", "#ffffff"}
        fallback_color = "#cfe2ff"
        device_colors: Dict[str, str] = {}
        color_idx = 0
        guard_sources = sorted({str(pos.get("source", name)) for name, pos in placement.items()})
        symmetry_groups: List[List[str]] = []
        if floorplan and guard_id in floorplan:
            symmetry_groups = [list(g) for g in floorplan[guard_id].get("symmetry_needed", [])]
        if not symmetry_groups:
            symmetry_groups = [[d1.name, d2.name] for d1, d2 in _collect_diff_pairs(self.circuit)]
        groups: List[List[str]] = []
        for group in symmetry_groups:
            group_names = [name.split("(")[0] for name in group if name.split("(")[0] in guard_sources]
            if len(group_names) >= 2:
                groups.append(group_names)

        total_colors = len(guard_sources) + len(groups)
        palette = _color_pool(max(total_colors, 1))

        for group in groups:
            if len(group) == 2:
                color = palette[color_idx % len(palette)]
                color_idx += 1
                for name in group:
                    if name not in device_colors:
                        device_colors[name] = color
            else:
                for name in group:
                    if name not in device_colors:
                        device_colors[name] = palette[color_idx % len(palette)]
                        color_idx += 1

        for name in guard_sources:
            if name not in device_colors:
                device_colors[name] = palette[color_idx % len(palette)]
                color_idx += 1
        return device_colors, background_color, background_set, fallback_color

    def plot_render_preview_lines(self, title: str | None = None) -> List[str]:
        grid_plan = self.layout_data.get("grid_plan", {})
        floorplan = self.layout_data.get("guard_rings", {})
        lines: List[str] = []
        if title:
            lines.append(f"\n=== Plot Preview: {title} ===")
        if not grid_plan:
            lines.append("\n--- Guard Ring Plot Preview ---")
            lines.append("  None found.")
            return lines
        device_map = {dev.name: dev for dev in self.circuit.devices}

        lines.append("\n--- Guard Ring Plot Preview ---")
        for guard_id, data in grid_plan.items():
            rows = int(data.get("rows", 0))
            cols = int(data.get("cols", 0))
            placement = data.get("placement", {})
            if not rows or not cols:
                continue
            sources_by_type: Dict[str, Set[str]] = defaultdict(set)
            for pos in placement.values():
                if pos.get("virtual"):
                    continue
                sources_by_type[str(pos.get("type") or "")].add(str(pos.get("source") or ""))
            device_colors, _, _, _ = self._build_guard_ring_color_map(guard_id, placement, floorplan)
            render_rows = _build_render_rows(
                rows,
                cols,
                placement,
                device_map,
                edge_label=True,
                enforce_even_cols=False,
            )
            lines.append(f"  Guard Ring: {guard_id}")
            lines.append(f"    Grid: {rows} x {cols}")
            lines.append(f"    Color Map: {device_colors}")
            lines.append(f"    Sources by type: { {k: sorted(v) for k, v in sources_by_type.items()} }")
            for row in render_rows:
                row_labels: List[str] = []
                for cell in row:
                    if cell.get("virtual"):
                        row_labels.append("X")
                        continue
                    source_name = str(cell.get("source") or cell.get("name") or "")
                    dev_type = str(cell.get("type") or "")
                    color = device_colors.get(source_name, "")
                    row_labels.append(f"{source_name}({dev_type}):{color}")
                lines.append(f"    {row_labels}")
        return lines

    def print_plot_render_preview(self, out_path: Path | None = None, title: str | None = None) -> None:
        lines = self.plot_render_preview_lines(title=title)
        self._build_preview_lines(lines)
        self._write_preview_lines(lines, out_path)
    def render_guard_ring_grid_graph(
        self,
        output_dir: str | Path | None = None,
        title: str | None = None,
    ) -> Figure | None:
        grid_plan = self.layout_data.get("grid_plan", {})
        floorplan = self.layout_data.get("guard_rings", {})
        if not grid_plan:
            print("\n--- Guard Ring Grid Graph ---")
            print("  None found.")
            return None

        device_map = {dev.name: dev for dev in self.circuit.devices}

        def _short_name(value: str) -> str:
            text = str(value or "")
            if len(text) <= 5:
                return text
            return f"{text[:2]}.{text[-2:]}"

        out_dir = Path(output_dir) if output_dir else (Path(__file__).parent / "constraints_grid_graphs")
        out_dir.mkdir(parents=True, exist_ok=True)

        print("\n--- Guard Ring Grid Graph ---")
        guard_items = [(guard_id, data) for guard_id, data in grid_plan.items() if data.get("rows") and data.get("cols")]
        if not guard_items:
            print("  None found.")
            return None

        fig, axes = plt.subplots(
            nrows=len(guard_items),
            ncols=1,
            figsize=(max(8, max(d.get("cols", 0) for _, d in guard_items) * 2.2), max(4, sum(d.get("rows", 0) for _, d in guard_items) * 1.9)),
        )
        if title:
            fig.suptitle(title, fontsize=12)
        if len(guard_items) == 1:
            axes = [axes]

        for ax, (guard_id, data) in zip(axes, guard_items):
            rows = int(data.get("rows", 0))
            cols = int(data.get("cols", 0))
            placement = data.get("placement", {})
            device_colors, background_color, background_set, fallback_color = self._build_guard_ring_color_map(
                guard_id,
                placement,
                floorplan,
            )

            render_rows = _build_render_rows(
                rows,
                cols,
                placement,
                device_map,
                edge_label=True,
                enforce_even_cols=False,
            )
            max_cols = max((len(row) for row in render_rows), default=0)

            ax.set_title(f"{guard_id}", fontsize=10)
            ax.set_aspect("equal", adjustable="box")
            ax.axis("off")
            ax.set_xlim(-0.7, max_cols - 0.3)
            ax.set_ylim(-0.6, rows - 0.4)

            for r, row in enumerate(render_rows):
                y = rows - 1 - r
                row_body_net = ""
                for cell in row:
                    dev = cell.get("device")
                    if dev is not None:
                        row_body_net = dev.b or dev.s or ""
                        break
                if not row_body_net:
                    row_body_net = "-"
                for c, cell in enumerate(row):
                    x = c
                    is_virtual = bool(cell.get("virtual"))
                    dev = cell.get("device")
                    if is_virtual or dev is None:
                        body_net = row_body_net
                        label = f"X\nb:{body_net}"
                        gate_net = body_net
                        face_color = background_color
                        edge_color = "#999"
                    else:
                        name_label = str(cell.get("name") or "")
                        type_label = str(cell.get("type") or "")
                        label = f"{name_label}\n{type_label}\nb:{dev.b or '-'}"
                        gate_net = dev.g or "-"
                        source_name = str(cell.get("source") or dev.name)
                        color = device_colors.get(source_name)
                        if color and color.lower() in background_set:
                            color = fallback_color
                        face_color = color if color else background_color
                        edge_color = color if color else "#222"
                    marker = "o" if is_virtual or dev is None else "s"
                    ax.scatter([x], [y], s=1000, marker=marker, facecolors=face_color, edgecolors=edge_color, linewidths=1.4)
                    ax.text(x, y, label, ha="center", va="center", fontsize=9)

                    ax.plot([x, x], [y + 0.05, y + 0.35], color="#444", linewidth=1.1)
                    ax.text(x + 0.02, y + 0.38, f"g:{gate_net}", ha="left", va="bottom", fontsize=7, rotation=90)

                    if c + 1 < len(row):
                        right = row[c + 1]
                        right_dev = right.get("device")
                        right_virtual = bool(right.get("virtual"))
                        net_label = cell.get("right_net") or row_body_net
                        edge_color = "#444"
                        left_source = str(cell.get("source") or (dev.name if dev else ""))
                        right_source = str(right.get("source") or (right_dev.name if right_dev else ""))
                        left_color = device_colors.get(left_source) if (not is_virtual and dev is not None) else None
                        right_color = device_colors.get(right_source) if right_dev else None
                        if left_color and left_color.lower() in background_set:
                            left_color = fallback_color
                        if right_color and right_color.lower() in background_set:
                            right_color = fallback_color
                        if left_color and right_color and left_color == right_color:
                            edge_color = left_color
                        elif left_color:
                            edge_color = left_color
                        elif right_color and right_virtual:
                            edge_color = right_color
                        ax.plot([x + 0.1, x + 0.9], [y, y], color=edge_color, linewidth=1.4)
                        ax.text(x + 0.5, y + 0.08, net_label, ha="center", va="bottom", fontsize=7)

                if row:
                    left_edge = row[0]
                    left_net = left_edge.get("left_net") or row_body_net
                    left_label = left_edge.get("left_label") or "s"
                    if left_edge.get("virtual"):
                        term_label = f"{left_label}:{left_net}"
                        ax.plot([-0.55, -0.1], [y, y], color="#444", linewidth=1.2)
                        ax.text(-0.32, y + 0.08, term_label, ha="center", va="bottom", fontsize=7)
                    else:
                        if left_net != row_body_net:
                            term_label = f"{left_label}:{left_net}"
                            ax.plot([-0.55, -0.1], [y, y], color="#444", linewidth=1.2)
                            ax.text(-0.32, y + 0.08, term_label, ha="center", va="bottom", fontsize=7)

                    right_edge = row[-1]
                    right_net = right_edge.get("right_net") or row_body_net
                    right_label = right_edge.get("right_label") or "d"
                    if right_edge.get("virtual"):
                        term_label = f"{right_label}:{right_net}"
                        ax.plot([max_cols - 0.9, max_cols - 0.45], [y, y], color="#444", linewidth=1.2)
                        ax.text(max_cols - 0.67, y + 0.08, term_label, ha="center", va="bottom", fontsize=7)
                    else:
                        if right_net != row_body_net:
                            term_label = f"{right_label}:{right_net}"
                            ax.plot([max_cols - 0.9, max_cols - 0.45], [y, y], color="#444", linewidth=1.2)
                            ax.text(max_cols - 0.67, y + 0.08, term_label, ha="center", va="bottom", fontsize=7)

            output_path = out_dir / f"{guard_id}_grid_graph.png"
            fig.tight_layout()
            fig.savefig(output_path, dpi=160)
            print(f"  Saved: {output_path}")

        return fig

    @staticmethod
    def show_figures_in_tabs(figures: List[Tuple[str, Figure]]) -> None:
        if not figures:
            return
        try:
            import tkinter as tk
            from tkinter import ttk
            # Optional backend import; may not exist in minimal environments.
            from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg  # type: ignore
        except Exception:
            print("Unable to open tabbed plot window; falling back to individual figures.")
            for _, fig in figures:
                fig.show()
            return

        root = tk.Tk()
        root.title("Guard Ring Grid Graphs")
        notebook = ttk.Notebook(root)
        notebook.pack(fill="both", expand=True)
        canvases: List[FigureCanvasTkAgg] = []

        def _close_all() -> None:
            for _, fig in figures:
                try:
                    plt.close(fig)
                except Exception:
                    pass
            try:
                root.quit()
            except Exception:
                pass
            try:
                root.destroy()
            except Exception:
                pass

        for title, fig in figures:
            frame = ttk.Frame(notebook)
            notebook.add(frame, text=title)
            canvas = FigureCanvasTkAgg(fig, master=frame)
            canvas.draw()
            canvas.get_tk_widget().pack(fill="both", expand=True)
            canvases.append(canvas)

        root.protocol("WM_DELETE_WINDOW", _close_all)
        root.bind("<Destroy>", lambda event: _close_all() if event.widget is root else None)
        root.mainloop()
        try:
            plt.close("all")
        except Exception:
            pass

    def _dump_edge_render_state(self) -> None:
        grid_plan = self.layout_data.get("grid_plan", {})
        device_map = {dev.name: dev for dev in self.circuit.devices}
        for guard_id, data in grid_plan.items():
            if not data.get("rows") or not data.get("cols"):
                continue
            render_rows = _build_render_rows(
                int(data.get("rows", 0)),
                int(data.get("cols", 0)),
                data.get("placement", {}),
                device_map,
                edge_label=True,
                enforce_even_cols=False,
            )
            print(f"\n--- Edge Debug Dump: {guard_id} ---")
            for r, row in enumerate(render_rows):
                row_body_net = "-"
                for cell in row:
                    dev = cell.get("device")
                    if dev is not None:
                        row_body_net = dev.b or dev.s or "-"
                        break
                lead = 0
                while lead < len(row) and row[lead].get("virtual"):
                    lead += 1
                trail = 0
                while trail < len(row) and row[len(row) - 1 - trail].get("virtual"):
                    trail += 1
                first_real = next((c for c in row if not c.get("virtual")), None)
                last_real = next((c for c in reversed(row) if not c.get("virtual")), None)
                left_dummy = row[0] if row else None
                right_dummy = row[-1] if row else None
                print(
                    f"  row {r}: body={row_body_net} lead={lead} trail={trail} "
                    f"left_dummy=({left_dummy.get('left_net') if left_dummy else None}/"
                    f"{left_dummy.get('right_net') if left_dummy else None}) "
                    f"right_dummy=({right_dummy.get('left_net') if right_dummy else None}/"
                    f"{right_dummy.get('right_net') if right_dummy else None}) "
                    f"first_real=({first_real.get('left_net') if first_real else None}/"
                    f"{first_real.get('right_net') if first_real else None}) "
                    f"last_real=({last_real.get('left_net') if last_real else None}/"
                    f"{last_real.get('right_net') if last_real else None})"
                )

    def _run_symmetry_dummy_tests(self) -> None:
        grid_plan = self.layout_data.get("grid_plan", {})
        floorplan = self.layout_data.get("guard_rings", {})
        device_map = {dev.name: dev for dev in self.circuit.devices}
        for guard_id, data in grid_plan.items():
            if not data.get("rows") or not data.get("cols"):
                continue
            symmetry_set: Set[str] = set()
            if floorplan and guard_id in floorplan:
                for group in floorplan[guard_id].get("symmetry_needed", []):
                    for name in group:
                        symmetry_set.add(name.split("(")[0])
            render_rows = _build_render_rows(
                int(data.get("rows", 0)),
                int(data.get("cols", 0)),
                data.get("placement", {}),
                device_map,
                edge_label=False,
                enforce_even_cols=True,
            )
            row_lengths = {len(row) for row in render_rows}
            if len(row_lengths) > 1:
                raise AssertionError(
                    f"{guard_id}: row lengths not rectangular {sorted(row_lengths)}"
                )
            max_cols = max((len(row) for row in render_rows), default=0)
            target_center = (max_cols - 1) / 2 if max_cols > 0 else 0
            for row in render_rows:
                row_body_net = "-"
                for cell in row:
                    dev = cell.get("device")
                    if dev is not None:
                        row_body_net = dev.b or dev.s or "-"
                        break

                # Dummy net rules at open edges.
                for idx, cell in enumerate(row):
                    if not cell.get("virtual"):
                        continue
                    left_neighbor = row[idx - 1] if idx - 1 >= 0 else None
                    right_neighbor = row[idx + 1] if idx + 1 < len(row) else None
                    if idx == 0 and (cell.get("left_net") or row_body_net) != row_body_net:
                        raise AssertionError(
                            f"{guard_id}: dummy left edge not body net ({cell.get('left_net')} != {row_body_net})"
                        )
                    if idx == len(row) - 1 and (cell.get("right_net") or row_body_net) != row_body_net:
                        raise AssertionError(
                            f"{guard_id}: dummy right edge not body net ({cell.get('right_net')} != {row_body_net})"
                        )
                    if left_neighbor is None or left_neighbor.get("virtual"):
                        if (cell.get("left_net") or row_body_net) != row_body_net:
                            raise AssertionError(
                                f"{guard_id}: dummy left net not body at edge"
                            )
                    if right_neighbor is None or right_neighbor.get("virtual"):
                        if (cell.get("right_net") or row_body_net) != row_body_net:
                            raise AssertionError(
                                f"{guard_id}: dummy right net not body at edge"
                            )

                # Symmetric padding: leading and trailing dummy counts must match.
                lead = 0
                while lead < len(row) and row[lead].get("virtual"):
                    lead += 1
                trail = 0
                while trail < len(row) and row[len(row) - 1 - trail].get("virtual"):
                    trail += 1
                if lead != trail:
                    raise AssertionError(
                        f"{guard_id}: row padding not symmetric (L{lead}/R{trail})"
                    )

                # Row center check only for rows containing symmetry devices.
                real_indices = []
                row_has_symmetry = False
                for i, cell in enumerate(row):
                    if cell.get("virtual"):
                        continue
                    real_indices.append(i)
                    dev = cell.get("device")
                    if dev and dev.name in symmetry_set:
                        row_has_symmetry = True
                if row_has_symmetry and real_indices:
                    center = (real_indices[0] + real_indices[-1]) / 2
                    if abs(center - target_center) > 0.001:
                        raise AssertionError(
                            f"{guard_id}: row center {center} != target {target_center}"
                        )

# ==========================================
# 2. SPICE Parser
# ==========================================

SPICE_SUFFIX_SCALE = {
    "f": 1e-15,
    "p": 1e-12,
    "n": 1e-9,
    "u": 1e-6,
    "m": 1e-3,
    "k": 1e3,
    "meg": 1e6,
    "g": 1e9,
}


def _parse_spice_value(value: str) -> float:
    match = re.match(r"^([+-]?\d*\.?\d+(?:[eE][+-]?\d+)?)([a-zA-Z]+)?$", value)
    if not match:
        return 0.0
    number, suffix = match.groups()
    base = float(number)
    if not suffix:
        return base
    scale = SPICE_SUFFIX_SCALE.get(suffix.lower())
    return base * scale if scale is not None else base


def _parse_multiplicity(params: str) -> int:
    if not params:
        return 1
    total = 1
    for key in ("m", "mult", "nf"):
        match = re.search(
            rf"\b{re.escape(key)}\s*=\s*([+-]?\d*\.?\d+(?:[eE][+-]?\d+)?)",
            params,
            re.IGNORECASE,
        )
        if match:
            try:
                value = float(match.group(1))
            except ValueError:
                continue
            count = int(round(value))
            if count > 0:
                total *= count
    return max(total, 1)


def _infer_device_type(model: str) -> str:
    model_lower = model.lower()
    if "pfet" in model_lower or "pmos" in model_lower:
        return "pmos"
    if "nfet" in model_lower or "nmos" in model_lower:
        return "nmos"
    if model_lower.startswith("p"):
        return "pmos"
    if model_lower.startswith("n"):
        return "nmos"
    return "nmos"


def parse_spice(netlist_text: str) -> Circuit:
    """
    Parses a raw SPICE netlist text into a Circuit object.
    Supports M-style MOSFET syntax: Mname D G S B Model W=... L=...
    Accepts XM-style prefixes used in some generated netlists.
    """
    circuit = Circuit()

    # Regex for W and L parameters
    w_pattern = re.compile(r"\bw\s*=\s*([+-]?\d*\.?\d+(?:[eE][+-]?\d+)?[a-zA-Z]*)", re.IGNORECASE)
    l_pattern = re.compile(r"\bl\s*=\s*([+-]?\d*\.?\d+(?:[eE][+-]?\d+)?[a-zA-Z]*)", re.IGNORECASE)

    for line in netlist_text.splitlines():
        line = line.strip()
        if not line or line.startswith('*') or line.startswith('.'):
            continue

        tokens = line.split()
        if len(tokens) < 6:
            continue

        name = tokens[0]
        name_lower = name.lower()
        if not (name_lower.startswith("m") or name_lower.startswith("xm")):
            continue

        d, g, s, b, model = tokens[1:6]
        params = " ".join(tokens[6:])

        dev_type = _infer_device_type(model)

        w_match = w_pattern.search(params)
        l_match = l_pattern.search(params)
        w_val = _parse_spice_value(w_match.group(1)) if w_match else 0.0
        l_val = _parse_spice_value(l_match.group(1)) if l_match else 0.0
        mult = _parse_multiplicity(params)

        device = Device(name, dev_type, d, g, s, b, w_val, l_val, mult, model)
        circuit.add_device(device)

    return circuit


# ==========================================
# 3. Detection Algorithms
# ==========================================

SUPPLY_NETS_HIGH = {"vdd", "vdd!", "vdda", "vddio", "vcc"}
SUPPLY_NETS_LOW = {"gnd", "gnd!", "vss", "vss!", "vssa", "vssio"}


def _is_supply_net(net: str) -> bool:
    return net.lower() in SUPPLY_NETS_HIGH or net.lower() in SUPPLY_NETS_LOW


def _is_high_supply(net: str) -> bool:
    return net.lower() in SUPPLY_NETS_HIGH


def _is_low_supply(net: str) -> bool:
    return net.lower() in SUPPLY_NETS_LOW


def _is_signal_net(net: str) -> bool:
    return not _is_supply_net(net)


def find_diode_connected(circuit: Circuit):
    """Finds devices where Gate is shorted to Drain."""
    print(f"\n--- Diode Connected Devices ---")
    count = 0
    for dev in circuit.devices:
        # Diode-connected MOS: gate tied to drain forces Vgs=Vds.
        # Line drawing (Legend: [M]=MOS, o=net, D/G/S labels are terminals):
        #   D=G o----+
        #           |
        #         |/|
        #         | |
        #         |/|
        #           |
        #           o S
        # Distinctive traits:
        #  - Gate and drain share the same net (self-biased).
        #  - Behaves like a MOS "diode" with exponential-ish I-V in subthreshold.
        # Analog use:
        #  - Generates a reference Vgs for mirrors, bias stacks, and startup circuits.
        #  - Used for level shifting and simple clamps in analog IO.
        if dev.d == dev.g:
            print(f"  Found: {dev.name} (Net: {dev.d})")
            count += 1
    if count == 0: print("  None found.")


def find_inverters(circuit: Circuit):
    """
    CMOS Inverter: PMOS and NMOS in series.
    Gates tied (Input), Drains tied (Output).
    """
    print(f"\n--- CMOS Inverters ---")
    processed = set()

    # Iterate through all NMOS devices
    for nmos in [d for d in circuit.devices if d.type == 'nmos']:
        if nmos in processed: continue

        # Distinctive traits:
        # Line drawing (CMOS inverter, Legend: [PMOS]/[NMOS]=devices, o=net):
        #   VDD o--[PMOS]--+-- OUT
        #                  |
        #   GND o--[NMOS]--+
        #             |
        #            IN (common gate)
        #  - PMOS and NMOS share the same gate (common input).
        #  - Drains tied together (common output), sources go to VDD/GND.
        #  - Complementary pull-up/pull-down action.
        # Analog use:
        #  - Level restoration and rail-to-rail buffering in mixed-signal paths.
        #  - Can serve as a digital threshold element or output stage.
        #  - Often part of bias comparators and startup circuits.
        candidates = [d for d in circuit.net_map[nmos.g] if d.type == 'pmos']

        for pmos in candidates:
            # Drains tied -> common output node.
            # If both conditions hold, this is a CMOS inverter.
            if pmos.d == nmos.d:
                print(f"  Found: Inv(N:{nmos.name}, P:{pmos.name}) Input:{nmos.g} Output:{nmos.d}")
                processed.add(nmos)
                processed.add(pmos)


def find_transmission_gates(circuit: Circuit):
    """
    Parallel PMOS and NMOS acting as a switch.
    Drain connected to Drain, Source connected to Source.
    """
    print(f"\n--- Transmission Gates (Switches) ---")
    processed = set()

    for nmos in [d for d in circuit.devices if d.type == 'nmos']:
        if nmos in processed: continue

        # Distinctive traits:
        # Line drawing (transmission gate, Legend: [NMOS]/[PMOS]=devices, o=net):
        #   N1 o--[NMOS]--o N2
        #       \       /
        #        \-[PMOS]-
        #   gates: clk / clk_b
        #  - PMOS/NMOS in parallel between the same two nodes.
        #  - Complementary gate control nets (clk / clk_b).
        #  - Low on-resistance across a wide signal range.
        # Analog use:
        #  - Sampling switches in S/H, muxes, and analog crossbars.
        #  - Reduces signal-dependent R_on compared to NMOS-only pass gates.
        candidates = [d for d in circuit.net_map[nmos.d] if d.type == 'pmos']

        for pmos in candidates:
            # A transmission gate is parallel NMOS/PMOS with the same two terminals.
            # Accept swapped drain/source because MOS terminals can be symmetric in pass devices.
            if (pmos.s == nmos.s) or (pmos.s == nmos.d and pmos.d == nmos.s):
                print(f"  Found: {nmos.name} || {pmos.name} (Nodes: {nmos.d}, {nmos.s})")
                processed.add(nmos)
                processed.add(pmos)


def find_diff_pairs(circuit: Circuit):
    """
    Differential Pair Criteria:
    1. Two devices of same type & dimensions.
    2. Sources connected to the same net (Tail).
    3. Gates are different nets (Inputs).
    4. Drains are different nets (Outputs).
    """
    print(f"\n--- Differential Pairs ---")
    processed = set()

    for d1 in circuit.devices:
        if d1 in processed: continue

        # Distinctive traits:
        # Line drawing (diff pair, Legend: [M]=device, o=net):
        #         D1 o---[M1]
        #              |
        #   IN1 o------+
        #              |
        #         D2 o---[M2]
        #              |
        #   IN2 o------+
        #              |
        #           Tail (shared source)
        #  - Matched devices (type, W/L) to improve common-mode rejection.
        #  - Shared source (tail) node driven by a current source.
        #  - Different gates (inputs) and different drains (outputs).
        # Analog use:
        #  - Primary input stage for op-amps and comparators.
        #  - Converts differential input into differential output currents.
        #  - Key to high CMRR and gain in analog front-ends.
        candidates = [d for d in circuit.net_map[d1.s] if d != d1]

        for d2 in candidates:
            if (d1.type == d2.type and
                    d1.w == d2.w and
                    d1.l == d2.l and
                    d1.g != d2.g and  # Different gates
                    d1.d != d2.d and  # Different drains
                    _is_signal_net(d1.g) and _is_signal_net(d2.g) and
                    _is_signal_net(d1.d) and _is_signal_net(d2.d)):
                # Same type & dimensions -> matched devices.
                # Shared source -> common tail current.
                # Different gates/drains -> two inputs and two outputs.
                print(f"  Found: {d1.name} & {d2.name} (Tail Net: {d1.s})")
                processed.add(d1)
                processed.add(d2)


def find_cross_coupled_pairs(circuit: Circuit):
    """
    SRAM/Latch Topology:
    Gate(A) == Drain(B) AND Gate(B) == Drain(A).
    """
    print(f"\n--- Cross-Coupled Pairs (Latches) ---")
    processed = set()

    for d1 in circuit.devices:
        if d1 in processed: continue

        # Distinctive traits:
        # Line drawing (cross-coupled, Legend: [M]=device, o=net, arrows=feedback):
        #   Q o----[M1]----o Qb
        #    ^      |      ^
        #    |      |      |
        #    +------[M2]---+
        #  - Mutual feedback: drain of each drives the other's gate.
        #  - Positive feedback creates two stable states.
        #  - Often appears in symmetric pairs (q/qb).
        # Analog use:
        #  - Latches, SRAM storage nodes, sense amplifiers.
        #  - Regenerative comparators for fast decision making.
        candidates = [d for d in circuit.net_map[d1.g] if d.d == d1.g and d != d1]

        for d2 in candidates:
            # Cross-coupled pair requires mutual feedback:
            # Gate(d1) <- Drain(d2) and Gate(d2) <- Drain(d1).
            if (d2.g == d1.d and
                    d1.type == d2.type and
                    _is_signal_net(d1.g) and _is_signal_net(d2.g) and
                    _is_signal_net(d1.d) and _is_signal_net(d2.d)):
                # Typically same type for core latches, but can be mixed in some oscillators
                print(f"  Found: {d1.name} & {d2.name} (Cross Nets: {d1.g}, {d2.g})")
                processed.add(d1)
                processed.add(d2)


def find_current_mirrors(circuit: Circuit):
    """
    Current Mirror Criteria:
    1. Same type.
    2. Gates tied together.
    3. Sources tied together.
    4. At least one device is Diode Connected (Reference).
    """
    print(f"\n--- Current Mirrors ---")
    processed = set()

    for d1 in circuit.devices:
        if d1 in processed: continue

        # Distinctive traits:
        # Line drawing (current mirror, Legend: [M]=device, o=net):
        #   VDD o---[M1]---o D1=G1 (ref)
        #          | |
        #          +-+---- G2
        #   VDD o---[M2]---o D2 (copy)
        #  - Same type and shared gate and source (mirrored Vgs).
        #  - One device diode-connected to set the reference current.
        #  - Output device(s) copy that current to other branches.
        # Analog use:
        #  - Bias networks, current references, active loads.
        #  - Enables current steering and gain boosting in amplifiers.
        candidates = [d for d in circuit.net_map[d1.g] if d != d1]

        for d2 in candidates:
            if (d1.type == d2.type and d1.s == d2.s):
                # At least one diode-connected device provides reference current.
                # The other device copies the current to a different drain net.
                # Identify Reference
                role = ""
                if d1.d == d1.g:
                    role = f"Ref: {d1.name}, Copy: {d2.name}"
                elif d2.d == d2.g:
                    role = f"Ref: {d2.name}, Copy: {d1.name}"

                if role:
                    print(f"  Found: {role} (Gate Net: {d1.g})")
                    processed.add(d1)
                    processed.add(d2)


def find_cascodes(circuit: Circuit):
    """
    Cascode Stack:
    Drain of M1 (Bottom) -> Source of M2 (Top).
    Same Type.
    """
    print(f"\n--- Cascode Stacks ---")

    for bot in circuit.devices:
        # Distinctive traits:
        # Line drawing (cascode stack, Legend: [M]=device, o=net):
        #   VDD o---[Top M2]---o
        #               |
        #             [Bot M1]
        #               |
        #             OUT/Node
        #  - Same type stacked devices.
        #  - Bottom drain connects to top source (series stack).
        #  - Top device gate usually at a bias (cascode bias net).
        # Analog use:
        #  - Boosts output resistance and gain.
        #  - Improves isolation from output voltage variations.
        potential_tops = circuit.net_map[bot.d]

        for top in potential_tops:
            if bot == top: continue

            # Check connection: Bot Drain -> Top Source
            if top.s == bot.d and top.type == bot.type:
                print(f"  Found: Bottom {bot.name} -> Top {top.name} (Node: {bot.d})")


def find_folded_cascodes(circuit: Circuit):
    """
    Folded Cascode:
    Drain of Bottom -> Source of Top, but devices are opposite types.
    """
    print(f"\n--- Folded Cascodes (Mixed-Type Stacks) ---")
    for bot in circuit.devices:
        # Distinctive traits:
        # Line drawing (folded cascode, Legend: [N]/[P]=type, o=net):
        #   VDD o---[P]---o Folded Node
        #               |
        #             [N]
        #               |
        #             OUT/Node
        #  - Series stack like a cascode, but opposite types.
        #  - Enables high gain with extended output swing.
        # Analog use:
        #  - Common in low-voltage op-amps and wide-swing amplifiers.
        potential_tops = circuit.net_map[bot.d]
        for top in potential_tops:
            if bot == top:
                continue
            if top.s == bot.d and top.type != bot.type:
                print(
                    f"  Found: Bottom {bot.name} ({bot.type}) -> Top {top.name} ({top.type}) "
                    f"(Node: {bot.d})"
                )


def find_source_followers(circuit: Circuit):
    """
    Source Follower (Common Drain):
    Drain tied to supply, gate is input, source is output.
    """
    print(f"\n--- Source Followers (Common Drain) ---")
    for dev in circuit.devices:
        # Distinctive traits:
        # Line drawing (source follower, Legend: [M]=device, o=net):
        #   VDD o---[M]---o OUT (source)
        #          |
        #         IN (gate)
        #  - Drain at supply, gate is input, source is output.
        #  - Provides level shift and buffering (near-unity gain).
        # Analog use:
        #  - Buffers, level shifters, and output drivers.
        if dev.type == "nmos" and _is_high_supply(dev.d):
            if not _is_supply_net(dev.g) and not _is_supply_net(dev.s):
                print(f"  Found: {dev.name} (NMOS follower, Out: {dev.s}, In: {dev.g})")
        elif dev.type == "pmos" and _is_low_supply(dev.d):
            if not _is_supply_net(dev.g) and not _is_supply_net(dev.s):
                print(f"  Found: {dev.name} (PMOS follower, Out: {dev.s}, In: {dev.g})")


def find_active_loads(circuit: Circuit):
    """
    Active Load:
    A current mirror device used as the load for a gain stage/diff pair output.
    """
    print(f"\n--- Active Loads (Mirror as Load) ---")
    # Collect differential pair devices to find their drain (output) nodes.
    diff_pairs: List[Tuple[Device, Device]] = []
    processed = set()
    for d1 in circuit.devices:
        if d1 in processed:
            continue
        candidates = [d for d in circuit.net_map[d1.s] if d != d1]
        for d2 in candidates:
            if (d1.type == d2.type and d1.w == d2.w and d1.l == d2.l and
                    d1.g != d2.g and d1.d != d2.d):
                diff_pairs.append((d1, d2))
                processed.add(d1)
                processed.add(d2)

    # Build mirror groups keyed by (type, gate_net, source_net)
    mirrors = defaultdict(list)
    for dev in circuit.devices:
        mirrors[(dev.type, dev.g, dev.s)].append(dev)

    found = False
    for d1, d2 in diff_pairs:
        for d in (d1, d2):
            # Look for opposite-type mirror devices whose drain is on the diff output node.
            for (m_type, m_gate, m_source), m_devs in mirrors.items():
                if m_type == d.type:
                    continue
                for m in m_devs:
                    if m.d == d.d:
                        # Require at least one diode-connected device in the mirror group.
                        if any(md.d == md.g for md in m_devs):
                            print(
                                f"  Found: Load {m.name} on {d.name} output "
                                f"(Out Net: {d.d}, Mirror Gate: {m.g})"
                            )
                            found = True
    if not found:
        print("  None found.")


def find_two_stage_amplifiers(circuit: Circuit):
    """
    Two-Stage Amplifier:
    Stage-1 diff pair -> Stage-2 common-source driven by a diff output.
    """
    print(f"\n--- Two-Stage Amplifier Candidates ---")
    diff_pairs: List[Tuple[Device, Device]] = []
    processed = set()
    for d1 in circuit.devices:
        if d1 in processed:
            continue
        candidates = [d for d in circuit.net_map[d1.s] if d != d1]
        for d2 in candidates:
            if (d1.type == d2.type and d1.w == d2.w and d1.l == d2.l and
                    d1.g != d2.g and d1.d != d2.d):
                diff_pairs.append((d1, d2))
                processed.add(d1)
                processed.add(d2)

    found = False
    for d1, d2 in diff_pairs:
        drain_nets = {d1.d, d2.d}
        for dev in circuit.devices:
            # Second stage: gate driven by a diff output net, drain elsewhere.
            if dev.g in drain_nets and dev.d not in drain_nets:
                print(
                    f"  Found: Diff pair ({d1.name},{d2.name}) drives {dev.name} "
                    f"(Gate: {dev.g}, Output: {dev.d})"
                )
                found = True
    if not found:
        print("  None found.")


def layout_requirement_catalog() -> Dict[str, Dict[str, object]]:
    """
    Physical design requirements per detected structure.
    The keys use consistent flags for placement and routing constraints.
    """
    return {
        "diode_connected": {
            "abutment_needed": False,
            "symmetry_needed": False,
            "common_centroid_needed": False,
            "interdigitation_needed": False,
            "matchlength_routing_needed": False,
            "guard_ring_recommended": True,
            "matched_device_dimensions_needed": False,
            "matched_orientation_needed": False,
        },
        "inverter": {
            "abutment_needed": True,
            "symmetry_needed": False,
            "common_centroid_needed": False,
            "interdigitation_needed": False,
            "matchlength_routing_needed": False,
            "guard_ring_recommended": False,
            "matched_device_dimensions_needed": False,
            "matched_orientation_needed": True,
        },
        "transmission_gate": {
            "abutment_needed": True,
            "symmetry_needed": True,
            "common_centroid_needed": False,
            "interdigitation_needed": True,
            "matchlength_routing_needed": True,
            "guard_ring_recommended": False,
            "matched_device_dimensions_needed": True,
            "matched_orientation_needed": True,
        },
        "diff_pair": {
            "abutment_needed": False,
            "symmetry_needed": True,
            "common_centroid_needed": True,
            "interdigitation_needed": True,
            "matchlength_routing_needed": True,
            "guard_ring_recommended": True,
            "matched_device_dimensions_needed": True,
            "matched_orientation_needed": True,
        },
        "cross_coupled": {
            "abutment_needed": False,
            "symmetry_needed": True,
            "common_centroid_needed": False,
            "interdigitation_needed": True,
            "matchlength_routing_needed": True,
            "guard_ring_recommended": True,
            "matched_device_dimensions_needed": True,
            "matched_orientation_needed": True,
        },
        "current_mirror": {
            "abutment_needed": False,
            "symmetry_needed": True,
            "common_centroid_needed": True,
            "interdigitation_needed": True,
            "matchlength_routing_needed": True,
            "guard_ring_recommended": True,
            "matched_device_dimensions_needed": True,
            "matched_orientation_needed": True,
        },
        "cascode": {
            "abutment_needed": False,
            "symmetry_needed": True,
            "common_centroid_needed": False,
            "interdigitation_needed": False,
            "matchlength_routing_needed": True,
            "guard_ring_recommended": True,
            "matched_device_dimensions_needed": True,
            "matched_orientation_needed": True,
        },
        "folded_cascode": {
            "abutment_needed": False,
            "symmetry_needed": True,
            "common_centroid_needed": False,
            "interdigitation_needed": False,
            "matchlength_routing_needed": True,
            "guard_ring_recommended": True,
            "matched_device_dimensions_needed": True,
            "matched_orientation_needed": True,
        },
        "source_follower": {
            "abutment_needed": False,
            "symmetry_needed": False,
            "common_centroid_needed": False,
            "interdigitation_needed": False,
            "matchlength_routing_needed": False,
            "guard_ring_recommended": False,
            "matched_device_dimensions_needed": False,
            "matched_orientation_needed": False,
        },
        "active_load": {
            "abutment_needed": False,
            "symmetry_needed": True,
            "common_centroid_needed": True,
            "interdigitation_needed": True,
            "matchlength_routing_needed": True,
            "guard_ring_recommended": True,
            "matched_device_dimensions_needed": True,
            "matched_orientation_needed": True,
        },
        "two_stage_amp": {
            "abutment_needed": False,
            "symmetry_needed": True,
            "common_centroid_needed": True,
            "interdigitation_needed": True,
            "matchlength_routing_needed": True,
            "guard_ring_recommended": True,
            "matched_device_dimensions_needed": True,
            "matched_orientation_needed": True,
        },
    }


def _pair_key(*names: str) -> Tuple[str, ...]:
    return tuple(sorted(names))


def _is_dummy_device(name: str) -> bool:
    return "dummy" in name.lower()


def _collect_diff_pairs(circuit: Circuit) -> List[Tuple[Device, Device]]:
    pairs = []
    seen = set()
    for d1 in circuit.devices:
        if _is_dummy_device(d1.name):
            continue
        candidates = [d for d in circuit.net_map[d1.s] if d != d1]
        for d2 in candidates:
            if _is_dummy_device(d2.name):
                continue
            if (d1.type == d2.type and d1.w == d2.w and d1.l == d2.l and
                    d1.g != d2.g and d1.d != d2.d and
                    _is_signal_net(d1.g) and _is_signal_net(d2.g) and
                    _is_signal_net(d1.d) and _is_signal_net(d2.d)):
                key = _pair_key(d1.name, d2.name)
                if key in seen:
                    continue
                seen.add(key)
                pairs.append((d1, d2))
    return pairs


def collect_structure_instances(circuit: Circuit) -> List[Dict[str, object]]:
    instances: List[Dict[str, object]] = []
    seen_instances: Set[Tuple[str, Tuple[str, ...]]] = set()

    for dev in circuit.devices:
        if _is_dummy_device(dev.name):
            continue
        if dev.d == dev.g:
            key = ("diode_connected", (dev.name,))
            if key not in seen_instances:
                seen_instances.add(key)
                instances.append({
                    "structure": "diode_connected",
                    "description": get_structure_description("diode_connected"),
                    "devices": [dev.name],
                    "nets": {"dg": dev.d, "s": dev.s},
                })

    for nmos in [d for d in circuit.devices if d.type == "nmos" and not _is_dummy_device(d.name)]:
        candidates = [d for d in circuit.net_map[nmos.g] if d.type == "pmos"]
        for pmos in candidates:
            if _is_dummy_device(pmos.name):
                continue
            if pmos.d == nmos.d:
                key = ("inverter", _pair_key(nmos.name, pmos.name))
                if key not in seen_instances:
                    seen_instances.add(key)
                    instances.append({
                        "structure": "inverter",
                    "description": get_structure_description("inverter"),
                        "devices": [nmos.name, pmos.name],
                        "nets": {"in": nmos.g, "out": nmos.d},
                    })

    for nmos in [d for d in circuit.devices if d.type == "nmos" and not _is_dummy_device(d.name)]:
        candidates = [d for d in circuit.net_map[nmos.d] if d.type == "pmos"]
        for pmos in candidates:
            if _is_dummy_device(pmos.name):
                continue
            if (pmos.s == nmos.s) or (pmos.s == nmos.d and pmos.d == nmos.s):
                key = ("transmission_gate", _pair_key(nmos.name, pmos.name))
                if key not in seen_instances:
                    seen_instances.add(key)
                    instances.append({
                        "structure": "transmission_gate",
                    "description": get_structure_description("transmission_gate"),
                        "devices": [nmos.name, pmos.name],
                        "nets": {"a": nmos.d, "b": nmos.s},
                    })

    for d1, d2 in _collect_diff_pairs(circuit):
        key = ("diff_pair", _pair_key(d1.name, d2.name))
        if key not in seen_instances:
            seen_instances.add(key)
            instances.append({
                "structure": "diff_pair",
                "description": get_structure_description("diff_pair"),
                "devices": [d1.name, d2.name],
                "nets": {"tail": d1.s, "inputs": [d1.g, d2.g], "outputs": [d1.d, d2.d]},
            })

    for d1 in circuit.devices:
        if _is_dummy_device(d1.name):
            continue
        candidates = [d for d in circuit.net_map[d1.g] if d.d == d1.g and d != d1]
        for d2 in candidates:
            if _is_dummy_device(d2.name):
                continue
            if (d2.g == d1.d and
                    d1.type == d2.type and
                    _is_signal_net(d1.g) and _is_signal_net(d2.g) and
                    _is_signal_net(d1.d) and _is_signal_net(d2.d)):
                key = ("cross_coupled", _pair_key(d1.name, d2.name))
                if key not in seen_instances:
                    seen_instances.add(key)
                    instances.append({
                        "structure": "cross_coupled",
                    "description": get_structure_description("cross_coupled"),
                        "devices": [d1.name, d2.name],
                        "nets": {"q": d1.d, "qb": d2.d},
                    })

    for d1 in circuit.devices:
        if _is_dummy_device(d1.name):
            continue
        candidates = [d for d in circuit.net_map[d1.g] if d != d1]
        for d2 in candidates:
            if _is_dummy_device(d2.name):
                continue
            if d1.type == d2.type and d1.s == d2.s:
                if d1.d == d1.g or d2.d == d2.g:
                    key = ("current_mirror", _pair_key(d1.name, d2.name))
                    if key not in seen_instances:
                        seen_instances.add(key)
                        instances.append({
                            "structure": "current_mirror",
                        "description": get_structure_description("current_mirror"),
                            "devices": [d1.name, d2.name],
                            "nets": {"gate": d1.g, "source": d1.s},
                        })

    for bot in circuit.devices:
        if _is_dummy_device(bot.name):
            continue
        for top in circuit.net_map[bot.d]:
            if bot == top:
                continue
            if _is_dummy_device(top.name):
                continue
            if top.s == bot.d and top.type == bot.type:
                key = ("cascode", _pair_key(bot.name, top.name))
                if key not in seen_instances:
                    seen_instances.add(key)
                    instances.append({
                        "structure": "cascode",
                        "description": get_structure_description("cascode"),
                        "devices": [bot.name, top.name],
                        "nets": {"node": bot.d},
                    })
            if top.s == bot.d and top.type != bot.type:
                key = ("folded_cascode", _pair_key(bot.name, top.name))
                if key not in seen_instances:
                    seen_instances.add(key)
                    instances.append({
                        "structure": "folded_cascode",
                        "description": get_structure_description("folded_cascode"),
                        "devices": [bot.name, top.name],
                        "nets": {"node": bot.d},
                    })

    for dev in circuit.devices:
        if _is_dummy_device(dev.name):
            continue
        if dev.type == "nmos" and _is_high_supply(dev.d):
            if not _is_supply_net(dev.g) and not _is_supply_net(dev.s):
                key = ("source_follower", (dev.name,))
                if key not in seen_instances:
                    seen_instances.add(key)
                    instances.append({
                        "structure": "source_follower",
                        "description": get_structure_description("source_follower"),
                        "devices": [dev.name],
                        "nets": {"in": dev.g, "out": dev.s},
                    })
        elif dev.type == "pmos" and _is_low_supply(dev.d):
            if not _is_supply_net(dev.g) and not _is_supply_net(dev.s):
                key = ("source_follower", (dev.name,))
                if key not in seen_instances:
                    seen_instances.add(key)
                    instances.append({
                        "structure": "source_follower",
                        "description": get_structure_description("source_follower"),
                        "devices": [dev.name],
                        "nets": {"in": dev.g, "out": dev.s},
                    })

    diff_pairs = _collect_diff_pairs(circuit)
    mirrors = defaultdict(list)
    for dev in circuit.devices:
        if _is_dummy_device(dev.name):
            continue
        mirrors[(dev.type, dev.g, dev.s)].append(dev)

    for d1, d2 in diff_pairs:
        for d in (d1, d2):
            for (m_type, _, _), m_devs in mirrors.items():
                if m_type == d.type:
                    continue
                for m in m_devs:
                    if _is_dummy_device(m.name):
                        continue
                    if m.d == d.d and any(md.d == md.g for md in m_devs):
                        key = ("active_load", _pair_key(m.name, d.name))
                        if key not in seen_instances:
                            seen_instances.add(key)
                            instances.append({
                                "structure": "active_load",
                                "description": get_structure_description("active_load"),
                                "devices": [m.name, d.name],
                                "nets": {"out": d.d, "mirror_gate": m.g},
                            })

    for d1, d2 in diff_pairs:
        drain_nets = {d1.d, d2.d}
        for dev in circuit.devices:
            if _is_dummy_device(dev.name):
                continue
            if dev.g in drain_nets and dev.d not in drain_nets:
                key = ("two_stage_amp", _pair_key(d1.name, d2.name, dev.name))
                if key not in seen_instances:
                    seen_instances.add(key)
                    instances.append({
                        "structure": "two_stage_amp",
                        "description": get_structure_description("two_stage_amp"),
                        "devices": [d1.name, d2.name, dev.name],
                        "nets": {"stage1_out": dev.g, "stage2_out": dev.d},
                    })

    return instances


def map_layout_requirements(circuit: Circuit) -> List[Dict[str, object]]:
    catalog = layout_requirement_catalog()
    mapped = []
    for inst in collect_structure_instances(circuit):
        req = catalog.get(inst["structure"], {})
        mapped.append({
            "structure": inst["structure"],
            "description": inst.get("description", get_structure_description(inst["structure"])),
            "devices": inst.get("devices", []),
            "nets": inst.get("nets", {}),
            "layout_requirements": req,
        })
    return mapped


def create_floorplan_guard_rings(
    circuit: Circuit,
    specs: List[Dict[str, object]] | None = None
) -> Dict[str, Dict[str, object]]:
    """
    Build a floorplan dictionary keyed by guard-ring identifier.
    Values include guard-ring type and requirement-to-instance mappings.
    """
    if specs is None:
        specs = map_layout_requirements(circuit)

    device_type_map = {dev.name: dev.type for dev in circuit.devices}
    device_body_map = {dev.name: (dev.b or dev.s) for dev in circuit.devices}

    floorplan: Dict[str, Dict[str, object]] = {
        "gr_nwell": {"grtype": "nwell", "devtype": "pmos"},
        "gr_pwell": {"grtype": "pwell", "devtype": "nmos"},
    }
    floorplan["_device_types"] = device_type_map
    floorplan["cross_gr_match"] = []
    symmetry_used = {"gr_nwell": set(), "gr_pwell": set()}

    for spec in specs:
        requirements = spec.get("layout_requirements", {})
        if not requirements.get("guard_ring_recommended", False):
            continue

        devices = [name for name in spec.get("devices", []) if not _is_dummy_device(name)]
        if not devices:
            continue

        devices_by_type_full: Dict[str, List[str]] = defaultdict(list)
        for name in devices:
            devices_by_type_full[device_type_map.get(name, "nmos")].append(name)

        if len(devices_by_type_full) > 1:
            floorplan["cross_gr_match"].append(list(devices))

        for req_key, req_val in requirements.items():
            if req_key == "guard_ring_recommended" or not req_val:
                continue
            if req_key == "symmetry_needed" and spec.get("structure") == "two_stage_amp":
                group_devices = devices[:2]
            else:
                group_devices = devices
            if not group_devices:
                continue
            devices_by_type: Dict[str, List[str]] = defaultdict(list)
            for name in group_devices:
                devices_by_type[device_type_map.get(name, "nmos")].append(name)
            for dev_type, group in devices_by_type.items():
                if dev_type == "pmos":
                    guard_ring_id = "gr_nwell"
                else:
                    guard_ring_id = "gr_pwell"
                bucket = floorplan[guard_ring_id]
                if req_key == "symmetry_needed":
                    if len(group) < 2:
                        continue
                    used = symmetry_used[guard_ring_id]
                    if any(name in used for name in group):
                        continue
                    used.update(group)
                existing = bucket.setdefault(req_key, [])
                group_key = tuple(sorted(group))
                if not any(tuple(sorted(g)) == group_key for g in existing):
                    existing.append(list(group))

    def _split_by_body_net(
        fp: Dict[str, Dict[str, object]],
        body_map: Dict[str, str],
    ) -> Dict[str, Dict[str, object]]:
        new_fp: Dict[str, Dict[str, object]] = {
            "_device_types": fp.get("_device_types", {}),
            "cross_gr_match": fp.get("cross_gr_match", []),
        }
        for guard_id, data in fp.items():
            if guard_id in {"_device_types", "cross_gr_match"}:
                continue
            for req_key, groups in data.items():
                if req_key in {"grtype", "devtype"}:
                    continue
                for group in groups:
                    by_body: Dict[str, List[str]] = defaultdict(list)
                    for name in group:
                        by_body[body_map.get(name, "")].append(name)
                    for body_net, members in by_body.items():
                        if not members:
                            continue
                        new_id = f"{guard_id}:{body_net}" if body_net else guard_id
                        bucket = new_fp.setdefault(new_id, {"grtype": data.get("grtype"), "devtype": data.get("devtype")})
                        existing = bucket.setdefault(req_key, [])
                        group_key = tuple(sorted(members))
                        if not any(tuple(sorted(g)) == group_key for g in existing):
                            existing.append(list(members))
        return new_fp

    return _split_by_body_net(floorplan, device_body_map)


def _net_terminal_map(circuit: Circuit) -> Dict[str, List[Tuple[str, str, str]]]:
    net_to_terms: Dict[str, List[Tuple[str, str, str]]] = defaultdict(list)
    for dev in circuit.devices:
        if _is_dummy_device(dev.name):
            continue
        if dev.d:
            net_to_terms[dev.d].append((dev.name, dev.type, "d"))
        if dev.g:
            net_to_terms[dev.g].append((dev.name, dev.type, "g"))
        if dev.s:
            net_to_terms[dev.s].append((dev.name, dev.type, "s"))
        if dev.b:
            net_to_terms[dev.b].append((dev.name, dev.type, "b"))
    return net_to_terms


def create_net_requirement_map(
    circuit: Circuit,
    specs: List[Dict[str, object]] | None = None,
) -> Dict[str, Dict[str, object]]:
    """
    Build net-centric groups for matched and symmetry requirements.
    matched_net_groupX: nets that require matched routing (from layout specs).
    symmetry_net_pairX: paired nets from differential pairs (gates/outputs).
    """
    result: Dict[str, Dict[str, object]] = {}
    net_to_terms = _net_terminal_map(circuit)

    if specs is None:
        specs = map_layout_requirements(circuit)

    matched_idx = 1
    for spec in specs:
        requirements = spec.get("layout_requirements", {})
        if not requirements.get("matchlength_routing_needed", False):
            continue
        nets = spec.get("nets", {})
        if not nets:
            continue
        group_nets: List[str] = []
        for value in nets.values():
            if isinstance(value, list):
                group_nets.extend(value)
            elif isinstance(value, str):
                group_nets.append(value)
        group_nets = [n for n in group_nets if n in net_to_terms]
        if len(group_nets) < 2:
            continue
        payload = {net: net_to_terms.get(net, []) for net in group_nets}
        result[f"matched_net_group{matched_idx}"] = payload
        matched_idx += 1

    pair_idx = 1
    for d1, d2 in _collect_diff_pairs(circuit):
        gate_pair = {
            d1.g: [(d1.name, d1.type, "g")],
            d2.g: [(d2.name, d2.type, "g")],
        }
        result[f"symmetry_net_pair{pair_idx}"] = gate_pair
        pair_idx += 1

        drain_pair = {
            d1.d: [(d1.name, d1.type, "d")],
            d2.d: [(d2.name, d2.type, "d")],
        }
        result[f"symmetry_net_pair{pair_idx}"] = drain_pair
        pair_idx += 1

    return result


def _grid_dimensions(count: int) -> Tuple[int, int]:
    cols = max(1, int(count ** 0.5))
    while cols * cols < count:
        cols += 1
    rows = (count + cols - 1) // cols
    return rows, cols


def _factor_pairs_no_ones(count: int) -> List[Tuple[int, int]]:
    pairs: List[Tuple[int, int]] = []
    for r in range(2, int(count ** 0.5) + 1):
        if count % r == 0:
            c = count // r
            if c > 1:
                pairs.append((r, c))
                if r != c:
                    pairs.append((c, r))
    return pairs


def _grid_dimensions_symmetry_priority(count: int, symmetry_priority: bool) -> Tuple[int, int]:
    pairs = _factor_pairs_no_ones(count)
    if not pairs:
        return _grid_dimensions(count)
    if symmetry_priority:
        pairs.sort(key=lambda rc: (rc[1] % 2, abs(rc[0] - rc[1])))
    else:
        pairs.sort(key=lambda rc: abs(rc[0] - rc[1]))
    return pairs[0]


def _load_force_shapes() -> Dict[str, Tuple[int, int]]:
    raw = os.getenv("CONSTRAINTS_FORCE_SHAPES", "").strip()
    if not raw:
        return {}
    try:
        data = json.loads(raw)
    except json.JSONDecodeError:
        return {}
    forced: Dict[str, Tuple[int, int]] = {}
    if isinstance(data, dict):
        for key, val in data.items():
            if isinstance(val, str) and "x" in val:
                parts = val.lower().split("x")
                if len(parts) == 2 and parts[0].isdigit() and parts[1].isdigit():
                    forced[str(key)] = (int(parts[0]), int(parts[1]))
            elif isinstance(val, (list, tuple)) and len(val) == 2:
                forced[str(key)] = (int(val[0]), int(val[1]))
    return forced


def _load_bool_env(key: str) -> bool:
    val = os.getenv(key, "").strip().lower()
    return val in {"1", "true", "yes", "y", "on"}


def _load_instance_overrides(
) -> Tuple[
    Dict[str, int],
    Set[str],
    List[Tuple[str, str]],
    Dict[str, Dict[str, int]],
    Dict[str, Set[str]],
]:
    add_raw = os.getenv("CONSTRAINTS_ADD_INSTANCES", "").strip()
    remove_raw = os.getenv("CONSTRAINTS_REMOVE_INSTANCES", "").strip()
    swap_raw = os.getenv("CONSTRAINTS_SWAP_INSTANCES", "").strip()
    add_group_raw = os.getenv("CONSTRAINTS_ADD_INSTANCES_BY_GROUP", "").strip()
    remove_group_raw = os.getenv("CONSTRAINTS_REMOVE_INSTANCES_BY_GROUP", "").strip()
    add: Dict[str, int] = {}
    remove: Set[str] = set()
    swaps: List[Tuple[str, str]] = []
    add_by_group: Dict[str, Dict[str, int]] = {}
    remove_by_group: Dict[str, Set[str]] = {}
    if add_raw:
        try:
            add = {str(k): int(v) for k, v in json.loads(add_raw).items()}
        except Exception:
            add = {}
    if remove_raw:
        try:
            remove = set(map(str, json.loads(remove_raw)))
        except Exception:
            remove = set()
    if swap_raw:
        try:
            for pair in json.loads(swap_raw):
                if isinstance(pair, (list, tuple)) and len(pair) == 2:
                    swaps.append((str(pair[0]), str(pair[1])))
        except Exception:
            swaps = []
    if add_group_raw:
        try:
            raw = json.loads(add_group_raw)
            if isinstance(raw, dict):
                for group_id, entries in raw.items():
                    if isinstance(entries, dict):
                        add_by_group[str(group_id)] = {
                            str(k): int(v) for k, v in entries.items()
                        }
        except Exception:
            add_by_group = {}
    if remove_group_raw:
        try:
            raw = json.loads(remove_group_raw)
            if isinstance(raw, dict):
                for group_id, entries in raw.items():
                    if isinstance(entries, (list, tuple, set)):
                        remove_by_group[str(group_id)] = set(map(str, entries))
        except Exception:
            remove_by_group = {}
    return add, remove, swaps, add_by_group, remove_by_group


def _allocate_symmetric_slots(
    available: Set[Tuple[int, int]],
    count: int,
    rows: int,
    cols: int,
) -> List[Tuple[int, int]]:
    for r in range(rows):
        row_slots = sorted([c for (rr, c) in available if rr == r])
        if len(row_slots) < count:
            continue
        chosen: List[Tuple[int, int]] = []
        left = 0
        right = len(row_slots) - 1
        while len(chosen) < count and left <= right:
            if len(chosen) + 1 == count and left == right:
                chosen.append((r, row_slots[left]))
                break
            chosen.append((r, row_slots[left]))
            if len(chosen) < count:
                chosen.append((r, row_slots[right]))
            left += 1
            right -= 1
        if len(chosen) == count:
            for slot in chosen:
                available.discard(slot)
            return chosen
    return []


def _allocate_row_major(
    available: Set[Tuple[int, int]],
    count: int,
) -> List[Tuple[int, int]]:
    chosen = sorted(available)[:count]
    for slot in chosen:
        available.discard(slot)
    return chosen


def _allocate_mirrored_pair(
    available: Set[Tuple[int, int]],
    rows: int,
    cols: int,
) -> List[Tuple[int, int]]:
    for r in range(rows):
        for c in range(cols // 2):
            left = (r, c)
            right = (r, cols - 1 - c)
            if left in available and right in available:
                available.discard(left)
                available.discard(right)
                return [left, right]
    return []


def _build_render_rows(
    rows: int,
    cols: int,
    placement: Dict[str, Dict[str, object]],
    device_map: Dict[str, Device],
    *,
    edge_label: bool = False,
    enforce_even_cols: bool = False,
) -> List[List[Dict[str, object]]]:
    def _orientations(dev: Device) -> List[Dict[str, str]]:
        return [
            {"left_label": "s", "left_net": dev.s, "right_label": "d", "right_net": dev.d},
            {"left_label": "d", "left_net": dev.d, "right_label": "s", "right_net": dev.s},
        ]

    def _resolve_row_cells(row_cells: List[Dict[str, object]]) -> List[Dict[str, object]]:
        resolved: List[Dict[str, object]] = []

        def _resolve_segment(segment: List[Dict[str, object]]):
            if not segment:
                return
            devices = []
            for cell in segment:
                name = str(cell.get("name", ""))
                source = str(cell.get("source") or name)
                dev = device_map.get(name) or device_map.get(source)
                devices.append((cell, dev))

            n = len(devices)
            dp = [[-1 for _ in range(2)] for _ in range(n)]
            prev_choice: List[List[int | None]] = [[None, None] for _ in range(n)]

            for o in range(2):
                dp[0][o] = 0

            for i in range(1, n):
                _, prev_dev = devices[i - 1]
                _, cur_dev = devices[i]
                if prev_dev is None or cur_dev is None:
                    dp[i][0] = dp[i][1] = 0
                    prev_choice[i][0] = prev_choice[i][1] = 0
                    continue
                prev_orients = _orientations(prev_dev)
                cur_orients = _orientations(cur_dev)
                for o in range(2):
                    best_score = -1
                    best_prev = 0
                    for p in range(2):
                        score = dp[i - 1][p]
                        if prev_orients[p]["right_net"] == cur_orients[o]["left_net"]:
                            score += 1
                        if score > best_score:
                            best_score = score
                            best_prev = p
                    dp[i][o] = best_score
                    prev_choice[i][o] = best_prev

            last_choice = 0 if dp[-1][0] >= dp[-1][1] else 1
            choices = [0] * n
            choices[-1] = last_choice
            for i in range(n - 1, 0, -1):
                choices[i - 1] = int(prev_choice[i][choices[i]] or 0)

            mismatch_flags = [False] * (n - 1)
            for i in range(1, n):
                _, prev_dev = devices[i - 1]
                _, cur_dev = devices[i]
                if prev_dev is None or cur_dev is None:
                    continue
                prev_orient = _orientations(prev_dev)[choices[i - 1]]
                cur_orient = _orientations(cur_dev)[choices[i]]
                if prev_orient["right_net"] != cur_orient["left_net"]:
                    mismatch_flags[i - 1] = True

            for i, flag in enumerate(list(mismatch_flags)):
                if not flag:
                    continue
                mirror_idx = (n - 2) - i
                if 0 <= mirror_idx < len(mismatch_flags):
                    mismatch_flags[mirror_idx] = True

            for idx, (cell, dev) in enumerate(devices):
                if dev is None:
                    resolved.append({"name": "X", "virtual": True})
                    continue
                orient = _orientations(dev)[choices[idx]]
                resolved.append(
                    {
                        "name": str(cell.get("name", "")),
                        "type": cell.get("type", ""),
                        "source": cell.get("source", cell.get("name", "")),
                        "device": dev,
                        "left_label": orient["left_label"],
                        "left_net": orient["left_net"],
                        "right_label": orient["right_label"],
                        "right_net": orient["right_net"],
                    }
                )
                if idx < len(mismatch_flags) and mismatch_flags[idx]:
                    resolved.append({"name": "X", "virtual": True, "inserted": True})

        segment: List[Dict[str, object]] = []
        for cell in row_cells:
            name = str(cell.get("name", ""))
            is_virtual = bool(cell.get("virtual"))
            source = str(cell.get("source") or name)
            dev = device_map.get(name) or device_map.get(source)
            if not name or is_virtual or dev is None:
                _resolve_segment(segment)
                segment = []
                resolved.append({"name": "X", "virtual": True})
                continue
            segment.append(cell)

        _resolve_segment(segment)
        return resolved

    grid: List[List[Dict[str, object]]] = [[{} for _ in range(cols)] for _ in range(rows)]
    for name, pos in placement.items():
        r = int(pos.get("row", 0))
        c = int(pos.get("col", 0))
        if 0 <= r < rows and 0 <= c < cols:
            grid[r][c] = {
                "name": name,
                "virtual": bool(pos.get("virtual")),
                "type": pos.get("type", ""),
                "source": pos.get("source", name),
            }

    render_rows: List[List[Dict[str, object]]] = []
    for r in range(rows):
        row_cells = []
        for c in range(cols):
            cell = grid[r][c] if grid[r][c] else {"name": "", "virtual": True}
            row_cells.append(cell)
        render_rows.append(_resolve_row_cells(row_cells))

    edge_flags: List[Tuple[bool, bool]] = []
    for row in render_rows:
        row_body_net = "-"
        for cell in row:
            dev = cell.get("device")
            if dev is not None:
                row_body_net = dev.b or dev.s or "-"
                break
        left_flag = False
        right_flag = False
        for cell in row:
            if not cell.get("virtual"):
                left_net = cell.get("left_net", "") or row_body_net
                if left_net != row_body_net:
                    left_flag = True
                break
        for cell in reversed(row):
            if not cell.get("virtual"):
                right_net = cell.get("right_net", "") or row_body_net
                if right_net != row_body_net:
                    right_flag = True
                break
        edge_flags.append((left_flag, right_flag))

    for idx, row in enumerate(render_rows):
        left_flag, right_flag = edge_flags[idx]
        if left_flag:
            dummy = {"name": "X", "virtual": True}
            if edge_label:
                dummy["edge_label"] = "left"
            row.insert(0, dummy)
        if right_flag:
            dummy = {"name": "X", "virtual": True}
            if edge_label:
                dummy["edge_label"] = "right"
            row.append(dummy)

    for row in render_rows:
        lead = 0
        while lead < len(row) and row[lead].get("virtual"):
            lead += 1
        trail = 0
        while trail < len(row) and row[len(row) - 1 - trail].get("virtual"):
            trail += 1
        if lead < trail:
            for _ in range(trail - lead):
                row.insert(0, {"name": "X", "virtual": True})
        elif trail < lead:
            for _ in range(lead - trail):
                row.append({"name": "X", "virtual": True})

    max_cols = max((len(row) for row in render_rows), default=0)
    if enforce_even_cols and max_cols % 2 != 0 and max_cols > 0:
        max_cols += 1
    if any(((max_cols - len(row)) % 2) != 0 for row in render_rows):
        max_cols += 1

    for row in render_rows:
        gap = max_cols - len(row)
        if gap <= 0:
            continue
        if gap % 2 != 0:
            insert_at = len(row) // 2
            row.insert(insert_at, {"name": "X", "virtual": True})
            gap -= 1
        left_add = gap // 2
        right_add = gap - left_add
        for _ in range(left_add):
            row.insert(0, {"name": "X", "virtual": True})
        for _ in range(right_add):
            row.append({"name": "X", "virtual": True})

    max_cols = max((len(row) for row in render_rows), default=0)
    for row in render_rows:
        gap = max_cols - len(row)
        if gap <= 0:
            continue
        if gap % 2 != 0:
            insert_at = len(row) // 2
            row.insert(insert_at, {"name": "X", "virtual": True})
            gap -= 1
        left_add = gap // 2
        right_add = gap - left_add
        for _ in range(left_add):
            row.insert(0, {"name": "X", "virtual": True})
        for _ in range(right_add):
            row.append({"name": "X", "virtual": True})

    def _edge_dummy_needed(row: List[Dict[str, object]], side: str) -> bool:
        row_body_net = "-"
        for cell in row:
            dev = cell.get("device")
            if dev is not None:
                row_body_net = dev.b or dev.s or "-"
                break
        if row_body_net == "-" or not row:
            return False
        if side == "left":
            for cell in row:
                if not cell.get("virtual"):
                    left_net = cell.get("left_net", "") or row_body_net
                    return left_net != row_body_net
        else:
            for cell in reversed(row):
                if not cell.get("virtual"):
                    right_net = cell.get("right_net", "") or row_body_net
                    return right_net != row_body_net
        return False

    def _leading_virtual_count(row: List[Dict[str, object]]) -> int:
        count = 0
        for cell in row:
            if cell.get("virtual"):
                count += 1
            else:
                break
        return count

    def _trailing_virtual_count(row: List[Dict[str, object]]) -> int:
        count = 0
        for cell in reversed(row):
            if cell.get("virtual"):
                count += 1
            else:
                break
        return count

    while render_rows:
        lead_counts = [_leading_virtual_count(row) for row in render_rows if row]
        if not lead_counts:
            break
        min_lead = min(lead_counts)
        need_left = any(_edge_dummy_needed(row, "left") for row in render_rows)
        if min_lead > 1 or (min_lead == 1 and not need_left):
            for row in render_rows:
                if row:
                    row.pop(0)
            continue
        break

    while render_rows:
        trail_counts = [_trailing_virtual_count(row) for row in render_rows if row]
        if not trail_counts:
            break
        min_trail = min(trail_counts)
        need_right = any(_edge_dummy_needed(row, "right") for row in render_rows)
        if min_trail > 1 or (min_trail == 1 and not need_right):
            for row in render_rows:
                if row:
                    row.pop()
            continue
        break

    for row in render_rows:
        row_body_net = "-"
        for cell in row:
            dev = cell.get("device")
            if dev is not None:
                row_body_net = dev.b or dev.s or "-"
                break
        for idx, cell in enumerate(row):
            if not cell.get("virtual"):
                continue
            left_neighbor = row[idx - 1] if idx - 1 >= 0 else None
            right_neighbor = row[idx + 1] if idx + 1 < len(row) else None
            left_net = row_body_net
            right_net = row_body_net
            if left_neighbor and not left_neighbor.get("virtual"):
                left_net = left_neighbor.get("right_net", "") or row_body_net
            if right_neighbor and not right_neighbor.get("virtual"):
                right_net = right_neighbor.get("left_net", "") or row_body_net
            cell["left_net"] = left_net
            cell["right_net"] = right_net
            cell["left_label"] = "s"
            cell["right_label"] = "d"

    return render_rows


def create_guard_ring_grid_floorplan(
    floorplan: Dict[str, Dict[str, object]],
    circuit: Circuit,
) -> Dict[str, Dict[str, object]]:
    """
    Build a rectangular grid placement per guard ring that attempts to
    honor symmetry/common-centroid/interdigitation with a simple heuristic.
    """
    result: Dict[str, Dict[str, object]] = {}
    device_types = floorplan.get("_device_types", {})
    device_map = {dev.name: dev for dev in circuit.devices}
    device_body_map = {dev.name: (dev.b or dev.s) for dev in circuit.devices}
    all_devices_by_type: Dict[str, List[str]] = defaultdict(list)
    for dev in circuit.devices:
        if _is_dummy_device(dev.name):
            continue
        all_devices_by_type[dev.type].append(dev.name)

    priority_keys = [
        "common_centroid_needed",
        "symmetry_needed",
        "interdigitation_needed",
        "matched_device_dimensions_needed",
        "matched_orientation_needed",
        "matchlength_routing_needed",
        "abutment_needed",
    ]

    def _config_for_groups(
        groups_by_key: Dict[str, List[List[str]]],
        fallback_devices: List[str],
    ) -> Dict[str, object]:
        relax_symmetry = _load_bool_env("CONSTRAINTS_RELAX_SYMMETRY")
        symmetry_enabled = bool(
            groups_by_key.get("symmetry_needed")
            or groups_by_key.get("common_centroid_needed")
            or groups_by_key.get("interdigitation_needed")
        )
        if relax_symmetry:
            symmetry_enabled = False
        symmetry_mode = "centroid" if groups_by_key.get("common_centroid_needed") else "horizontal"
        match_group = None
        for group in groups_by_key.get("matchlength_routing_needed", []):
            if len(group) >= 2:
                match_group = group
                break
        label_group = match_group
        if not label_group:
            for key in ("symmetry_needed", "common_centroid_needed", "interdigitation_needed"):
                for group in groups_by_key.get(key, []):
                    if len(group) >= 2:
                        label_group = group
                        break
                if label_group:
                    break
        if not label_group and len(fallback_devices) >= 2:
            label_group = list(fallback_devices[:2])
        match_rules = {
            "enabled": bool(match_group),
            "A": label_group[0] if label_group else "A",
            "B": label_group[1] if label_group else "B",
            "match_weight": 1000,
            "prefer_rmst": True,
            "fallback_without_rmst": True,
            "apply_in_expansion": bool(match_group),
        }
        return {
            "symmetry": {
                "enabled": symmetry_enabled,
                "mode": symmetry_mode,
            },
            "match_rules": match_rules,
        }

    force_shapes = _load_force_shapes()
    allow_extra_rows_env = _load_bool_env("CONSTRAINTS_ALLOW_EXTRA_ROWS")
    compact_mode_env = _load_bool_env("CONSTRAINTS_COMPACT")
    add_instances, remove_instances, swap_instances, add_by_group, remove_by_group = _load_instance_overrides()
    for guard_id, data in floorplan.items():
        if guard_id in {"_device_types", "cross_gr_match"}:
            continue
        groups_by_key: Dict[str, List[List[str]]] = {}
        for req_key, groups in data.items():
            if req_key in {"grtype", "devtype"}:
                continue
            groups_by_key[req_key] = [list(g) for g in groups]

        devices: List[str] = []
        guard_devtype = str(data.get("devtype", ""))
        guard_body_net = ""
        if ":" in guard_id:
            _, guard_body_net = guard_id.split(":", 1)
        remove_effective = set(remove_instances)
        remove_effective.update(remove_by_group.get(guard_id, set()))
        for name in all_devices_by_type.get(guard_devtype, []):
            if name in remove_effective:
                continue
            if guard_body_net and device_body_map.get(name, "") != guard_body_net:
                continue
            if name not in devices:
                devices.append(name)
        for groups in groups_by_key.values():
            for group in groups:
                for name in group:
                    if name in remove_effective:
                        continue
                    if guard_body_net and device_body_map.get(name, "") != guard_body_net:
                        continue
                    if name not in devices:
                        devices.append(name)

        if not devices:
            continue

        # Drop removed devices from group definitions to avoid dangling matches.
        for key, groups in list(groups_by_key.items()):
            filtered_groups = []
            for group in groups:
                filtered = [d for d in group if d in devices]
                if filtered:
                    filtered_groups.append(filtered)
            groups_by_key[key] = filtered_groups

        symmetry_pairs = []
        seen_pairs: Set[Tuple[str, str]] = set()
        for group in groups_by_key.get("symmetry_needed", []):
            if len(group) != 2:
                continue
            name1, name2 = group
            if name1 in devices and name2 in devices:
                key = _pair_key(name1, name2)
                if key not in seen_pairs:
                    seen_pairs.add(key)
                    symmetry_pairs.append((name1, name2))
        for d1, d2 in _collect_diff_pairs(circuit):
            if d1.name in devices and d2.name in devices:
                key = _pair_key(d1.name, d2.name)
                if key not in seen_pairs:
                    seen_pairs.add(key)
                    symmetry_pairs.append((d1.name, d2.name))

        rows, cols = _grid_dimensions_symmetry_priority(len(devices), bool(symmetry_pairs))
        forced = force_shapes.get(guard_id)
        forced_applied = False
        forced_rows = None
        if forced:
            f_rows, f_cols = forced
            if f_rows > 1 and f_cols > 1 and f_rows * f_cols >= len(devices):
                rows, cols = f_rows, f_cols
                forced_applied = True
                forced_rows = f_rows
        if symmetry_pairs and cols % 2 != 0 and not forced_applied:
            cols += 1
            rows = (len(devices) + cols - 1) // cols
        if forced_applied and forced_rows and rows < forced_rows:
            rows = forced_rows
        available = {(r, c) for r in range(rows) for c in range(cols)}
        placement: Dict[str, Dict[str, object]] = {}
        unplaced: Set[str] = set(devices)

        for d1, d2 in symmetry_pairs:
            if d1 not in unplaced or d2 not in unplaced:
                continue
            slots = _allocate_mirrored_pair(available, rows, cols)
            if not slots:
                if forced_applied:
                    if forced_rows and rows < forced_rows:
                        rows += 1
                        for c in range(cols):
                            available.add((rows - 1, c))
                        slots = _allocate_mirrored_pair(available, rows, cols)
                    if not slots:
                        continue
                rows += 1
                for c in range(cols):
                    available.add((rows - 1, c))
                slots = _allocate_mirrored_pair(available, rows, cols)
            if not slots:
                continue
            for dev, (r, c) in zip([d1, d2], slots):
                placement[dev] = {
                    "row": r,
                    "col": c,
                    "type": device_types.get(dev, ""),
                }
                unplaced.discard(dev)

        dummy_idx = 1
        for key in priority_keys:
            for group in groups_by_key.get(key, []):
                group = [d for d in group if d in unplaced]
                if not group:
                    continue
                if key in {
                    "symmetry_needed",
                    "common_centroid_needed",
                    "interdigitation_needed",
                    "matchlength_routing_needed",
                } and len(group) % 2 != 0:
                    dummy_name = f"X_{guard_id}_{dummy_idx}"
                    dummy_idx += 1
                    group.append(dummy_name)
                slots: List[Tuple[int, int]]
                if key in {"symmetry_needed", "common_centroid_needed"}:
                    slots = _allocate_symmetric_slots(available, len(group), rows, cols)
                elif key == "interdigitation_needed":
                    slots = _allocate_symmetric_slots(available, len(group), rows, cols)
                else:
                    slots = _allocate_row_major(available, len(group))
                if len(slots) < len(group):
                    slots += _allocate_row_major(available, len(group) - len(slots))
                for dev, (r, c) in zip(group, slots):
                    if dev.startswith("X_"):
                        placement[dev] = {
                            "row": r,
                            "col": c,
                            "type": guard_devtype,
                            "virtual": True,
                        }
                        continue
                    placement[dev] = {
                        "row": r,
                        "col": c,
                        "type": device_types.get(dev, ""),
                    }
                    unplaced.discard(dev)

        if unplaced:
            leftover_slots = _allocate_row_major(available, len(unplaced))
            for dev, (r, c) in zip(sorted(unplaced), leftover_slots):
                placement[dev] = {
                    "row": r,
                    "col": c,
                    "type": device_types.get(dev, ""),
                }

        # Fill remaining empty slots with dummy devices to keep a full rectangle.
        if available:
            fill_idx = 1
            for (r, c) in sorted(available):
                fill_name = f"X_{guard_id}_fill_{fill_idx}"
                placement[fill_name] = {
                    "row": r,
                    "col": c,
                    "type": guard_devtype,
                    "virtual": True,
                }
                fill_idx += 1

        # Align multiplicity across matched/symmetric groups.
        match_keys = {
            "symmetry_needed",
            "common_centroid_needed",
            "interdigitation_needed",
            "matchlength_routing_needed",
            "matched_device_dimensions_needed",
            "matched_orientation_needed",
        }
        mult_overrides: Dict[str, int] = {}
        for key in match_keys:
            for group in groups_by_key.get(key, []):
                group_names = [name for name in group if name in device_map]
                if len(group_names) < 2:
                    continue
                max_mult = max(int(device_map[name].mult) for name in group_names)
                for name in group_names:
                    mult_overrides[name] = max_mult
        base_rows = [["X" for _ in range(cols)] for _ in range(rows)]
        for name, pos in placement.items():
            r = int(pos.get("row", 0))
            c = int(pos.get("col", 0))
            if 0 <= r < rows and 0 <= c < cols:
                base_rows[r][c] = "X" if pos.get("virtual") else name

        extra_counts: Dict[str, int] = {}
        has_mult = False
        for name in sorted({cell for row in base_rows for cell in row} - {"X"}):
            mult = mult_overrides.get(name) or (device_map.get(name).mult if name in device_map else 1)
            if int(mult) > 1:
                has_mult = True
                extra_counts[name] = int(mult) - 1
        group_adds = add_by_group.get(guard_id, {})
        valid_devices = set(devices)
        for name, count in add_instances.items():
            if name not in valid_devices:
                continue
            if name in extra_counts:
                extra_counts[name] += max(0, int(count))
            elif name in devices:
                extra_counts[name] = max(0, int(count))
                if extra_counts[name] > 0:
                    has_mult = True
        for name, count in group_adds.items():
            if name not in valid_devices:
                continue
            if name in extra_counts:
                extra_counts[name] += max(0, int(count))
            elif name in devices:
                extra_counts[name] = max(0, int(count))
                if extra_counts[name] > 0:
                    has_mult = True

        config = _config_for_groups(groups_by_key, devices)
        connectivity_rules = connectivity_rules_from_rows(base_rows)
        expanded_rows = base_rows
        if has_mult:
            expanded_rows = solve_universal_expansion(
                base_rows,
                extra_counts,
                connectivity_rules,
                match_rules=config.get("match_rules", {}),
                rmst_weight=0,
                dummy_penalty_weight=10,
                buffer_cols=0,
                enforce_even_cols=True,
                allow_extra_rows=allow_extra_rows_env,
                buffer_rows=0,
                allow_column_swaps=True,
                swap_penalty_weight=100000,
                symmetry_mode=config.get("symmetry", {}).get("mode") if config.get("symmetry", {}).get("enabled") else None,
                compact_mode=compact_mode_env,
                compact_penalty_weight=20,
            )
        if not expanded_rows:
            expanded_rows = base_rows

        expanded_rows_count = len(expanded_rows)
        expanded_cols = len(expanded_rows[0]) if expanded_rows else 0
        expanded_placement: Dict[str, Dict[str, object]] = {}
        name_counts: Dict[str, int] = defaultdict(int)
        instance_idx = 1
        dummy_idx = 1
        for r, row in enumerate(expanded_rows):
            for c, name in enumerate(row):
                if name == "X":
                    dummy_name = f"X_{guard_id}_exp_{dummy_idx}"
                    dummy_idx += 1
                    expanded_placement[dummy_name] = {
                        "row": r,
                        "col": c,
                        "type": guard_devtype,
                        "virtual": True,
                        "source": "X",
                    }
                    continue
                name_counts[name] += 1
                instance_name = name if name_counts[name] == 1 else f"I{instance_idx}"
                if name_counts[name] > 1:
                    instance_idx += 1
                expanded_placement[instance_name] = {
                    "row": r,
                    "col": c,
                    "type": device_types.get(name, guard_devtype),
                    "source": name,
                }

        if swap_instances:
            for left, right in swap_instances:
                if left in expanded_placement and right in expanded_placement:
                    expanded_placement[left], expanded_placement[right] = (
                        expanded_placement[right],
                        expanded_placement[left],
                    )

        result[guard_id] = {
            "rows": expanded_rows_count,
            "cols": expanded_cols,
            "placement": expanded_placement,
            "grtype": data.get("grtype"),
            "devtype": data.get("devtype"),
            "modgen_rows": expanded_rows,
            "connectivity_rules": connectivity_rules,
            "config": config,
        }

    return result


# ==========================================
# 4. Main Execution
# ==========================================

def load_local_netlist(relative_path: str) -> str:
    # Default search order:
    #  1) Absolute path (if user passes one)
    #  2) $VLSI_NETLIST_DIR (if set)
    #  3) Adjacent to this script (historical layout)
    #  4) Repo-level EDA data area: "EDA data/vlsi/netlists"
    p = Path(relative_path)
    if p.is_absolute() and p.exists():
        return p.read_text(encoding="utf-8")

    candidates = []
    env_dir = os.getenv("VLSI_NETLIST_DIR", "").strip()
    if env_dir:
        candidates.append(Path(env_dir) / relative_path)

    candidates.append(Path(__file__).parent / relative_path)

    # Try repo root heuristic: .../programming/python_programs/optimizations/constraints.py
    # -> .../programming/vlsi/data/eda_data/netlists
    try:
        repo_root = Path(__file__).resolve().parents[2]  # programming/
        candidates.append(repo_root / "vlsi" / "data" / "eda_data" / "netlists" / Path(relative_path).name)
        candidates.append(repo_root / "vlsi" / "data" / "eda_data" / "netlists" / relative_path)
    except Exception:
        pass

    for c in candidates:
        if c.exists():
            return c.read_text(encoding="utf-8")

    raise FileNotFoundError(f"Netlist not found: {relative_path}. Tried: " + ", ".join(str(x) for x in candidates))


def _timing_enabled() -> bool:
    return os.getenv("CONSTRAINTS_TIMING", "").strip().lower() in {"1", "true", "yes", "y", "on"}


def _time_block(label: str):
    start = time.perf_counter()
    return label, start


def _time_end(label: str, start: float):
    elapsed = time.perf_counter() - start
    print(f"[timing] {label}: {elapsed:.2f}s")


def print_qor_reports(reports: List[Tuple[str, Dict[str, object]]]) -> None:
    print("\n=== Quality of Results (QoR) ===")
    for name, report in reports:
        summary = report.get("summary", {})
        total = summary.get("total_checks", 0)
        failed = summary.get("failed_checks", 0)
        print(f"\n{name}")
        print(f"  Checks: {total} | Failed: {failed}")
        for guard_id, guard_report in report.get("guard_rings", {}).items():
            reqs = guard_report.get("requirements", [])
            failed_reqs = [r for r in reqs if r.get("status") == "fail"]
            if not failed_reqs:
                continue
            print(f"  Guard Ring: {guard_id}")
            for req in failed_reqs:
                group = req.get("group", [])
                issues = req.get("issues", [])
                print(f"    {req.get('requirement')}: {group}")
                for issue in issues:
                    print(f"      - {issue}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Constraints analyzer")
    parser.add_argument("--print", dest="print_output", action="store_true", help="Print full design data")
    parser.add_argument("--plot", action="store_true", help="Show plots in a tabbed window")
    parser.add_argument("--saveimage", action="store_true", help="Save plot images to constraints_grid_graphs/")
    parser.add_argument("--dumpjson", action="store_true", help="Dump full design data to JSON")
    parser.add_argument("--printqor", action="store_true", help="Print QoR reports only")
    parser.add_argument("--only", action="append", help="Run only matching netlist (name or filename)")
    args = parser.parse_args()

    if args.print_output:
        args.printqor = True

    test_netlist = load_local_netlist("netlists/test_netlist.sp")
    netlist_opamp = load_local_netlist("netlists/opamp_netlist.sp")
    netlist_bmr = load_local_netlist("netlists/beta_multiplier_netlist.sp")
    netlist_cml = load_local_netlist("netlists/cml_latch_netlist.sp")
    netlist_opamp_miller = load_local_netlist("netlists/opamp_miller_sky130.sp")
    netlist_opamp_ttsky25a = load_local_netlist("netlists/opamp_2stage_ttsky25a.sp")
    netlist_synth_folded = load_local_netlist("netlists/opamp_synth_folded_cascode.sp")
    netlist_synth_two_stage = load_local_netlist("netlists/opamp_synth_two_stage.sp")
    netlist_synth_diffpair = load_local_netlist("netlists/analog_synth_diffpair_active_load.sp")
    netlist_synth_mirror = load_local_netlist("netlists/analog_synth_current_mirror.sp")
    netlist_synth_cascode = load_local_netlist("netlists/analog_synth_cascode_mirror.sp")
    examples = {
        "Test Netlist": test_netlist,
        "OpAmp": netlist_opamp,
        "Beta Multiplier": netlist_bmr,
        "CML Latch": netlist_cml,
        "OpAmp (Miller, SKY130)": netlist_opamp_miller,
        "OpAmp (2-stage, ttsky25a)": netlist_opamp_ttsky25a,
        "OpAmp (Synth, Folded Cascode)": netlist_synth_folded,
        "OpAmp (Synth, Two-Stage)": netlist_synth_two_stage,
        "Diff Pair (Synth, Active Load)": netlist_synth_diffpair,
        "Current Mirror (Synth)": netlist_synth_mirror,
        "Cascode Mirror (Synth)": netlist_synth_cascode,
    }

    if args.only:
        only = {item.strip().lower() for item in args.only if item.strip()}
        file_map = {
            "test_netlist.sp": "Test Netlist",
            "opamp_netlist.sp": "OpAmp",
            "beta_multiplier_netlist.sp": "Beta Multiplier",
            "cml_latch_netlist.sp": "CML Latch",
            "opamp_miller_sky130.sp": "OpAmp (Miller, SKY130)",
            "opamp_2stage_ttsky25a.sp": "OpAmp (2-stage, ttsky25a)",
            "opamp_synth_folded_cascode.sp": "OpAmp (Synth, Folded Cascode)",
            "opamp_synth_two_stage.sp": "OpAmp (Synth, Two-Stage)",
            "analog_synth_diffpair_active_load.sp": "Diff Pair (Synth, Active Load)",
            "analog_synth_current_mirror.sp": "Current Mirror (Synth)",
            "analog_synth_cascode_mirror.sp": "Cascode Mirror (Synth)",
        }
        only_keys = {k for k in examples if k.lower() in only}
        only_keys.update({file_map.get(name) for name in only if name in file_map})
        only_keys.discard(None)
        examples = {k: v for k, v in examples.items() if k in only_keys}

    qor_reports: List[Tuple[str, Dict[str, object]]] = []
    dump_data: Dict[str, object] = {}
    plot_queue: List[Tuple[str, Design]] = []

    def run_all() -> None:
        for name, netlist in examples.items():
            if _timing_enabled():
                label, start = _time_block(f"{name} parse_spice")
                ckt = parse_spice(netlist)
                _time_end(label, start)
            else:
                ckt = parse_spice(netlist)
            design = Design(ckt)
            if _timing_enabled():
                label, start = _time_block(f"{name} distill_schematic_data")
                design.distill_schematic_data()
                _time_end(label, start)
                label, start = _time_block(f"{name} distill_layout_data")
                design.distill_layout_data()
                _time_end(label, start)
            else:
                design.distill_schematic_data()
                design.distill_layout_data()

            if not design.run_layout_tests():
                if args.printqor:
                    qor_reports.append((name, {"summary": {"total_checks": 1, "failed_checks": 1},
                                               "guard_rings": {"layout_tests": {"requirements": [{
                                                   "requirement": "layout_tests",
                                                   "group": [],
                                                   "status": "fail",
                                                   "issues": ["layout tests failed"],
                                               }]}}}))
                continue

            if args.print_output:
                print(f"\n{'=' * 20} Analyzing {name} {'=' * 20}")
                print(f"Analyzed {len(ckt.devices)} devices.")
                design.print_design_data()

            if args.dumpjson:
                dump_data[name] = design.export_data()

            if args.printqor:
                qor_reports.append((name, design.qualityTest()))

            if args.plot or args.saveimage:
                plot_queue.append((name, design))

        if args.plot or args.saveimage:
            figures: List[Tuple[str, Figure]] = []
            for name, design in plot_queue:
                if _timing_enabled():
                    label, start = _time_block(f"{name} render_guard_ring_grid_graph")
                    fig = design.render_guard_ring_grid_graph(title=name)
                    _time_end(label, start)
                else:
                    fig = design.render_guard_ring_grid_graph(title=name)
                if fig is not None:
                    figures.append((name, fig))
            if args.saveimage:
                out_dir = Path(__file__).parent / "constraints_grid_graphs"
                out_dir.mkdir(parents=True, exist_ok=True)
                for name, fig in figures:
                    safe_name = re.sub(r"[^A-Za-z0-9_.-]+", "_", name).strip("_")
                    fig_path = out_dir / f"{safe_name or 'netlist'}.png"
                    fig.savefig(fig_path, dpi=200, bbox_inches="tight")
            if args.plot:
                Design.show_figures_in_tabs(figures)
            for _, fig in figures:
                plt.close(fig)
        else:
            preview_path = Path(__file__).parent / "constraints_plot_preview.txt"
            preview_lines: List[str] = []
            for name, design in plot_queue:
                lines = design.plot_render_preview_lines(title=name)
                preview_lines.extend(lines)
                design._build_preview_lines(lines)
            if preview_lines:
                preview_path.write_text("\n".join(preview_lines).rstrip() + "\n", encoding="utf-8")

    if args.print_output:
        run_all()
    else:
        with contextlib.redirect_stdout(io.StringIO()):
            run_all()

    if args.dumpjson:
        dump_path = Path(__file__).parent / "constraints_design_dump.json"
        dump_path.write_text(json.dumps(dump_data, indent=2), encoding="utf-8")

    if args.printqor:
        print_qor_reports(qor_reports)
