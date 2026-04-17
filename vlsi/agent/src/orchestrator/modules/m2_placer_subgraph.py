"""
agent/modules/m2_placer_subgraph.py  —  Module m2: Placer Subgraph
═══════════════════════════════════════════════════════════════════════════════

WHAT THIS MODULE IS:
  The LangGraph subgraph for PLACEMENT operations — deciding where standard
  cells and macro blocks should be placed on the chip floorplan.

  Placement is a prerequisite for routing: you can only draw wires AFTER you
  know where every cell is.

INTERNAL FLOW:
  validate → call_placer_via_cli → format_placer_response

WHAT THE C++ PLACER DOES (once implemented in eda_placer library):
  - Reads cell dimensions and connectivity from SharedDatabase
  - Uses simulated annealing or force-directed placement
  - Writes final (x, y) coordinates back to SharedDatabase
  - Returns: overlap_count, HPWL (half-perimeter wire length estimate), runtime_ms

COUPLING RULE:
  m2 never imports from m1, m3, or m4.
  If placement needs design context, Graph A passes it through PlacerState.
"""

import logging
from typing import TypedDict, Annotated, Sequence, Any
import operator

from langchain_core.messages import BaseMessage, AIMessage, HumanMessage
from langchain_google_genai import ChatGoogleGenerativeAI
from langgraph.graph import StateGraph, START, END

from ..utils.env_bootstrap import gemini_api_key
from ._cli_client import mcp_call, ping
from orchestrator.utils.spice_to_analog_problem import build_analog_problem_from_spice
from orchestrator.utils.early_router import build_early_routes

logger = logging.getLogger(__name__)


class PlacerState(TypedDict):
    """State for the Placer module subgraph."""
    messages:       Annotated[Sequence[BaseMessage], operator.add]
    placement_params: dict[str, Any]   # e.g. {"algorithm": "simulated_annealing"}
    placement_result: dict[str, Any]   # {"overlap_count": int, "hpwl": float}
    analog_problem: dict[str, Any]     # optional tool-neutral problem used for placement (for visualization/routing)
    error:          str


async def validate_placement_input(state: PlacerState) -> dict:
    """Check daemon is alive and design is loaded before placement."""
    daemon_alive = await ping()
    if not daemon_alive:
        return {"error": "EDA daemon not running. Start it first."}
    try:
        db_status = await mcp_call("db.status")
        if not db_status.get("is_loaded", False):
            return {"error": "No design loaded. Load a design first."}
    except RuntimeError as e:
        return {"error": str(e)}
    return {"error": ""}


async def call_placer_via_cli(state: PlacerState) -> dict:
    """
    Send 'place_cells' request to the C++ eda_cli daemon.

    WHAT HAPPENS IN C++:
      PlacerMcpServer::place_cells() is invoked.
      Calls into the eda_placer algorithm library (simulated annealing or
      force-directed placement), writes cell coordinates to SharedDatabase,
      returns placement metrics.

    NOTE: Until eda_placer is implemented, the daemon returns a stub response.
    """
    if state.get("error"):
        return {}
    params = dict(state.get("placement_params", {}))
    analog_problem = None

    # Tool-neutral analog path: allow passing a SPICE netlist and convert it into
    # the C++ analog_problem schema on the fly.
    if "spice_netlist_path" in params and "analog_problem" not in params:
        netlist_path = str(params.pop("spice_netlist_path"))
        outline = params.pop("outline", None)
        constraints_py = params.pop("constraints_py", None)
        analog_problem = await build_analog_problem_from_spice(
            netlist_path,
            outline=outline,
            constraints_py=constraints_py,
        )
        params = {
            "analog_problem": analog_problem,
            "options": params.get("options", params.get("placer_options", {})) or {},
        }
    logger.info("[Placer] Calling place_cells with params: %s", params)
    try:
        result = await mcp_call("place_cells", params)
        out = {"placement_result": result}
        if analog_problem is not None:
            out["analog_problem"] = analog_problem
        return out
    except RuntimeError as e:
        return {"error": str(e), "placement_result": {}}


async def format_placer_response(state: PlacerState) -> dict:
    """Use LLM to narrate placement results or errors as plain English."""
    llm = ChatGoogleGenerativeAI(
        model="gemini-2.0-flash",
        temperature=0.3,
        api_key=gemini_api_key(),
    )
    if state.get("error"):
        prompt = (
            f"VLSI placement failed: {state['error']}\n"
            f"Explain the problem and next steps in simple language."
        )
        return {"messages": [llm.invoke([HumanMessage(content=prompt)])]}

    r = state.get("placement_result", {})
    viewer_commands: list[dict[str, Any]] = []

    placed = r.get("placed") or []
    if isinstance(placed, list) and placed:
        viewer_commands.append({"action": "draw_instances", "instances": placed})
        analog_problem = state.get("analog_problem")
        if isinstance(analog_problem, dict) and analog_problem.get("nets"):
            viewer_commands.append({"action": "draw_routes", **build_early_routes(analog_problem=analog_problem, placed=placed, net_limit=60)})
        viewer_commands.append({"action": "zoom_fit"})
        viewer_commands.append({"action": "refresh_view"})

    prompt = (
        f"VLSI cell placement completed:\n"
        f"  Overlap count: {r.get('overlap_count', 'N/A')}\n"
        f"  HPWL estimate: {r.get('hpwl', 'N/A')} µm\n"
        f"  Runtime:       {r.get('runtime_ms', 'N/A')} ms\n"
        f"Summarise for a chip designer in 2-3 sentences. "
        f"If overlaps > 0, suggest running legalization."
    )
    return {"messages": [llm.invoke([HumanMessage(content=prompt)])], "viewer_commands": viewer_commands}


def create_placer_subgraph() -> StateGraph:
    """Builds and compiles the Placer module subgraph (m2)."""
    wf = StateGraph(PlacerState)
    wf.add_node("validate", validate_placement_input)
    wf.add_node("call_cli", call_placer_via_cli)
    wf.add_node("format",   format_placer_response)
    wf.add_edge(START,      "validate")
    wf.add_edge("validate", "call_cli")
    wf.add_edge("call_cli", "format")
    wf.add_edge("format",   END)
    return wf.compile()
