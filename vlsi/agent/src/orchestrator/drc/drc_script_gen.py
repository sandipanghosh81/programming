"""
drc_script_gen.py  —  Generate KLayout Ruby DRC scripts from JSON tech rules
─────────────────────────────────────────────────────────────────────────────
Takes the JSON output of the extract_tech_rules EU (min-width, min-space,
via enclosure rules, etc.) and produces a KLayout Ruby DRC script that can
be run headlessly in the KLayout Docker container.

WHY RUBY (not Python)?
  KLayout's DRC engine API is only available in Ruby scripting (it uses the
  DRC::Engine class).  The Python API (klayout.db) does not expose the same
  high-level DRC primitives.  We generate the Ruby script from Python, then
  execute it via klayout_client.py.

GENERATED SCRIPT STRUCTURE:
  source(input("/eda_share/routing_NNN.oas", 1))
  # One rule per constraint from tech_rules JSON
  metal2 = input(21, 0)
  metal2.width(0.070.um).output("M2_width", "M2 minimum width violation")
  metal2.space(0.070.um).output("M2_space", "M2 minimum spacing violation")
  # ... more rules ...
  report("DRC Report", "/eda_share/drc_report_NNN.rdb")
"""

from __future__ import annotations

import json
import logging
from typing import Any

logger = logging.getLogger(__name__)


def generate_drc_script(
    tech_rules: dict[str, Any],
    oasis_path: str,
    report_path: str,
) -> str:
    """
    Generate a KLayout Ruby DRC script from extracted techfile rules.

    Args:
        tech_rules:  JSON dict from the extract_tech_rules EU.
                     Expected structure:
                     {
                       "layers": [
                         {"name": "M1", "layer": 10, "datatype": 0,
                          "min_width_um": 0.060, "min_space_um": 0.060,
                          "min_area_um2": 0.01},
                         ...
                       ],
                       "via_rules": [
                         {"cut_layer": 15, "cut_datatype": 0,
                          "enc_m1_um": 0.030, "enc_m2_um": 0.030,
                          "min_cut_space_um": 0.120},
                         ...
                       ]
                     }
        oasis_path:  Path to the OASIS file on the shared volume.
        report_path: Path where KLayout should write the .rdb report.

    Returns:
        Ruby DRC script string.
    """
    lines: list[str] = []
    lines.append(f'source(input("{oasis_path}", 1))')
    lines.append("")

    # ── Layer rule checks ─────────────────────────────────────────────────────
    for layer_rule in tech_rules.get("layers", []):
        name     = layer_rule.get("name",          "UNNAMED")
        layer    = layer_rule.get("layer",         0)
        datatype = layer_rule.get("datatype",       0)
        width    = layer_rule.get("min_width_um",   None)
        space    = layer_rule.get("min_space_um",   None)
        area     = layer_rule.get("min_area_um2",   None)

        var = f"layer_{name.lower().replace('-', '_')}"
        lines.append(f"{var} = input({layer}, {datatype})")

        if width is not None:
            lines.append(
                f'{var}.width({width:.4f}.um).output("{name}_width", "{name} min width violation")'
            )
        if space is not None:
            lines.append(
                f'{var}.space({space:.4f}.um).output("{name}_space", "{name} min space violation")'
            )
        if area is not None:
            lines.append(
                f'{var}.area({area:.6f}.um2).output("{name}_area", "{name} min area violation")'
            )
        lines.append("")

    # ── Via enclosure checks ──────────────────────────────────────────────────
    for i, via_rule in enumerate(tech_rules.get("via_rules", [])):
        cut_layer    = via_rule.get("cut_layer",        0)
        cut_datatype = via_rule.get("cut_datatype",     0)
        enc_m1       = via_rule.get("enc_m1_um",        None)
        enc_m2       = via_rule.get("enc_m2_um",        None)
        cut_space    = via_rule.get("min_cut_space_um", None)

        vvar = f"via_{i}"
        lines.append(f"{vvar} = input({cut_layer}, {cut_datatype})")

        if cut_space is not None:
            lines.append(
                f'{vvar}.space({cut_space:.4f}.um).output("VIA{i}_cut_space", "Via cut spacing violation")'
            )
        lines.append("")

    # ── Report ────────────────────────────────────────────────────────────────
    lines.append(f'report("EDA Router DRC Check", "{report_path}")')

    script = "\n".join(lines)
    logger.debug("[DRC Script] Generated %d lines for %d layer rules, %d via rules",
                 len(lines),
                 len(tech_rules.get("layers", [])),
                 len(tech_rules.get("via_rules", [])))
    return script


def load_tech_rules(json_path: str) -> dict[str, Any]:
    """Load tech rules JSON file from the shared volume."""
    with open(json_path, "r", encoding="utf-8") as f:
        return json.load(f)
