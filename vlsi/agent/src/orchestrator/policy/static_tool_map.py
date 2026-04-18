"""
static_tool_map.py  —  Keyword-to-tool fallback map
─────────────────────────────────────────────────────────────────────────────
Used when ChromaDB (RAG service) is unavailable.  Maps EDA operation
keywords to (host, language, template_basename) tuples.

The map is intentionally conservative — only well-known deterministic
mappings are encoded.  If a keyword is unknown the caller falls back to
the host specified in the server environment variable EDA_DEFAULT_HOST.

MAINTENANCE NOTE:
  Add entries here whenever a new EU template is added to eu_registry/.
  The keys are lower-cased tokens extracted from the user's intent_params.
"""

from __future__ import annotations
from typing import NamedTuple

class ToolEntry(NamedTuple):
    host:     str   # "cadence_virtuoso" | "synopsys_icc2" | "klayout"
    language: str   # "skill" | "tcl" | "python"
    template: str   # basename under eu_registry/{host}/{template}.j2

# ── Static map ────────────────────────────────────────────────────────────────
# Keys must be lower-case, single-token operation names.
STATIC_MAP: dict[str, ToolEntry] = {
    # Routing operations
    "route_nets":          ToolEntry("cadence_virtuoso", "skill", "route_nets"),
    "route":               ToolEntry("cadence_virtuoso", "skill", "route_nets"),
    "global_route":        ToolEntry("cadence_virtuoso", "skill", "route_nets"),

    # Placement operations
    "place_cells":         ToolEntry("cadence_virtuoso", "skill", "place_cells"),
    "place":               ToolEntry("cadence_virtuoso", "skill", "place_cells"),
    "analog_place":        ToolEntry("cadence_virtuoso", "skill", "place_cells"),

    # Tech extraction
    "extract_tech_rules":  ToolEntry("cadence_virtuoso", "skill", "extract_tech_rules"),
    "extract_via_tech":    ToolEntry("cadence_virtuoso", "skill", "extract_via_tech"),

    # ICC2 equivalents
    "icc2_route":          ToolEntry("synopsys_icc2",    "tcl",   "route_nets"),
    "icc2_place":          ToolEntry("synopsys_icc2",    "tcl",   "place_cells"),
    "icc2_tech":           ToolEntry("synopsys_icc2",    "tcl",   "extract_tech_rules"),

    # KLayout view / DRC
    "zoom_highlight":      ToolEntry("klayout",          "python","zoom_and_highlight"),
    "highlight":           ToolEntry("klayout",          "python","zoom_and_highlight"),
    "zoom":                ToolEntry("klayout",          "python","zoom_and_highlight"),
    "drc_check":           ToolEntry("klayout",          "python","zoom_and_highlight"),  # viewer only
}


def lookup(operation: str) -> ToolEntry | None:
    """
    Return the static ToolEntry for an operation keyword, or None if unknown.
    Lookup is case-insensitive.
    """
    return STATIC_MAP.get(operation.lower())


def all_operations() -> list[str]:
    """Return all registered operation keywords (sorted)."""
    return sorted(STATIC_MAP.keys())
