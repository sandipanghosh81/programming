from __future__ import annotations

from pathlib import Path
from typing import Any

from orchestrator.utils.constraints_mcp_client import mcp_call_constraints


def _load_constraints_module(constraints_py: str | None = None):
    """
    Load the existing SPICE constraint extraction module (constraints.py) without
    requiring it to be installed as a package.
    """
    import sys
    import importlib.util

    if constraints_py is None:
        # Default to the constraints tool copy under vlsi/eda_tools.
        constraints_py = "/Users/sandipanghosh/programming/vlsi/eda_tools/python/constraints_tool/constraints.py"

    def _detect_repo_root(start: Path) -> Path:
        # Walk upward until we find the monorepo root markers.
        for d in [start] + list(start.parents):
            if (d / "cpp_programs").exists() and (d / "python_programs").exists():
                return d
        return start.parents[len(start.parents) - 1]

    # If a relative path is passed, interpret it relative to repo root.
    p = Path(constraints_py)
    if not p.is_absolute():
        repo_root = _detect_repo_root(Path(__file__).resolve())
        p = repo_root / constraints_py
    if not p.exists():
        raise FileNotFoundError(f"constraints.py not found at {p}")

    # Ensure sibling modules (e.g., modgen_scaling_rmst.py) are importable.
    sys.path.insert(0, str(p.parent))

    spec = importlib.util.spec_from_file_location("constraints", p)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load constraints module from {p}")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _variant_from_device(dev) -> dict[str, Any]:
    """
    Tool-neutral geometry approximation.
    - width roughly proportional to (W * mult)
    - height roughly proportional to L
    Pins placed on edges for routing estimation.
    """
    mult = int(getattr(dev, "mult", 1) or 1)
    w = float(getattr(dev, "w", 1.0) or 1.0)
    l = float(getattr(dev, "l", 1.0) or 1.0)

    # Keep geometry within a reasonable numeric range.
    ww = max(0.2, w * mult)
    hh = max(0.2, l)

    return {
        "name": "v0",
        "w": ww,
        "h": hh,
        "pins": [
            {"name": "D", "x": ww * 0.5, "y": hh},
            {"name": "S", "x": ww * 0.5, "y": 0.0},
            {"name": "G", "x": 0.0,     "y": hh * 0.5},
            {"name": "B", "x": ww,      "y": hh * 0.5},
        ],
        "allowed_orientations": ["R0", "R180", "MX", "MY"],
    }


async def build_analog_problem_from_spice(
    netlist_path: str,
    *,
    outline: dict[str, float] | None = None,
    constraints_py: str | None = None,
) -> dict[str, Any]:
    """
    Convert a SPICE netlist into the tool-neutral analog_problem schema expected
    by the C++ placer (eda_placer::analog).
    """
    # Preferred: ask the constraints MCP tool (tool-neutral, runs out-of-process).
    data = None
    try:
        data = await mcp_call_constraints("constraints.extract", {"spice_path": netlist_path})
    except Exception:
        data = None

    # Fallback: local import of constraints.py (no MCP server required).
    if data is None:
        mod = _load_constraints_module(constraints_py)
        text = Path(netlist_path).read_text(encoding="utf-8")
        circuit = mod.parse_spice(text)
        instances: list[dict[str, Any]] = []
        for dev in circuit.devices:
            instances.append(
                {"id": dev.name, "device_type": dev.type, "variants": [_variant_from_device(dev)]}
            )

        nets: dict[str, dict[str, Any]] = {}
        for net_name, devs in circuit.net_map.items():
            if not net_name:
                continue
            nets[net_name] = {"name": net_name, "weight": 1.0, "pins": []}
            for dev in devs:
                for term, pin in (("d", "D"), ("g", "G"), ("s", "S"), ("b", "B")):
                    if getattr(dev, term, "") == net_name:
                        nets[net_name]["pins"].append({"inst": dev.name, "pin": pin})

        sym_pairs: list[dict[str, str]] = []
        try:
            for d1, d2 in mod.find_diff_pairs(circuit):
                sym_pairs.append({"a": d1.name, "b": d2.name})
        except Exception:
            pass
    else:
        # Build from MCP result: use schematic_data.instances and schematic_data.nets if present.
        sch = (data or {}).get("schematic_data", {})
        inst_map = sch.get("instances", {}) or {}
        net_map = sch.get("nets", {}) or {}

        instances = []
        for inst_id, meta in inst_map.items():
            instances.append(
                {
                    "id": inst_id,
                    "device_type": meta.get("type", ""),
                    "variants": [
                        {
                            "name": "v0",
                            "w": float(meta.get("w", 1.0) or 1.0) * float(meta.get("multiplicity", 1) or 1),
                            "h": float(meta.get("l", 1.0) or 1.0),
                            "pins": [
                                {"name": "D", "x": 0.5, "y": 1.0},
                                {"name": "S", "x": 0.5, "y": 0.0},
                                {"name": "G", "x": 0.0, "y": 0.5},
                                {"name": "B", "x": 1.0, "y": 0.5},
                            ],
                            "allowed_orientations": ["R0", "R180", "MX", "MY"],
                        }
                    ],
                }
            )

        nets = {}
        for net_name, pins in net_map.items():
            nets[net_name] = {"name": net_name, "weight": 1.0, "pins": []}
            for pr in pins:
                inst = pr.get("instance")
                term = pr.get("terminal")
                if not inst or not term:
                    continue
                pin = {"d": "D", "g": "G", "s": "S", "b": "B"}.get(str(term).lower(), "G")
                nets[net_name]["pins"].append({"inst": inst, "pin": pin})

        # Try to derive symmetry pairs from detected structures if present.
        sym_pairs = []
        structs = sch.get("structures", {}) or {}
        for item in structs.get("diff_pair", []) if isinstance(structs, dict) else []:
            a = item.get("devices", [None, None])[0] if isinstance(item, dict) else None
            b = item.get("devices", [None, None])[1] if isinstance(item, dict) else None
            if a and b:
                sym_pairs.append({"a": a, "b": b})

    n = max(1, len(instances))
    if outline is None:
        # Heuristic outline for early routing/placement only.
        # For real flows, pass an explicit outline from the agent / KLayout context.
        side = max(50.0, (n ** 0.5) * 5.0)
        outline = {"w": float(side), "h": float(side)}

    return {
        "outline": outline,
        "instances": instances,
        "nets": list(nets.values()),
        "symmetry": {
            "vertical": True,
            # axis omitted => placer uses outline.w / 2
            "pairs": sym_pairs,
        },
    }

