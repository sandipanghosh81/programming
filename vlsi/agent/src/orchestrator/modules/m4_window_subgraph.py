"""
agent/modules/m4_window_subgraph.py  —  Module m4: Window / View Automation Subgraph
═══════════════════════════════════════════════════════════════════════════════

WHAT THIS MODULE IS:
  The LangGraph subgraph for KLayout VIEW operations that are driven from the
  Python side back into KLayout.

  Two communication directions:
    KLayout → Python agent:   User types "zoom to VDD"
    Python agent → KLayout:   server.py returns viewer_commands in the response
                              KLayout macro executes them locally

  This subgraph ASSEMBLES the viewer_commands list that server.py sends back.
  It may also call the C++ daemon for window-context queries (e.g., "what
  is currently visible?", "what is selected?").

INTERNAL FLOW:
  parse_view_request → (optionally) query_current_view → build_viewer_commands

VIEWER COMMANDS (returned in server.py ChatResponse.viewer_commands):
  {"action": "refresh_view"}
  {"action": "zoom_to",   "bbox": [x1,y1,x2,y2]}
  {"action": "highlight", "net_name": "VDD", "color": "#ff0000"}
  {"action": "zoom_fit"}
  {"action": "layer_visibility", "layer": "M1", "visible": true}
  {"action": "screenshot", "filename": "routing_result.png"}

COUPLING RULE:
  m4 never imports from m1, m2, or m3.
"""

import logging
from typing import TypedDict, Annotated, Sequence, Any
import operator

from langchain_core.messages import BaseMessage, HumanMessage
from langchain_google_genai import ChatGoogleGenerativeAI
from langgraph.graph import StateGraph, START, END

from ._cli_client import mcp_call, ping

logger = logging.getLogger(__name__)


class WindowState(TypedDict):
    """State for the Window Automation module subgraph."""
    messages:         Annotated[Sequence[BaseMessage], operator.add]
    view_request:     str           # e.g. "zoom_to_net VDD" or "refresh"
    view_params:      dict[str, Any]
    viewer_commands:  list[dict[str, Any]]  # Assembled command list for KLayout
    error:            str


# Map of plain-English view actions to structured commands.
# Expanded programmatically in parse_view_request.
_SIMPLE_ACTIONS = {
    "refresh":    [{"action": "refresh_view"}],
    "zoom_fit":   [{"action": "zoom_fit"}],
    "screenshot": [],  # needs filename from view_params
}


async def parse_view_request(state: WindowState) -> dict:
    """
    Parse the view_request string into a preliminary list of viewer_commands.
    Complex requests (e.g., "zoom to net VDD") need a DB lookup in the next node.
    Simple ones (refresh, zoom_fit) are resolved immediately.
    """
    req = state.get("view_request", "").strip().lower()

    if not req:
        return {"error": "No view request specified."}

    # Handle simple one-shot requests
    for key, cmds in _SIMPLE_ACTIONS.items():
        if key in req:
            if key == "screenshot":
                filename = state.get("view_params", {}).get("filename", "snapshot.png")
                return {"viewer_commands": [{"action": "screenshot", "filename": filename}],
                        "error": ""}
            return {"viewer_commands": cmds, "error": ""}

    # Complex: needs further processing (bbox lookup from DB, layer name resolution)
    return {"viewer_commands": [], "error": ""}


async def resolve_view_parameters(state: WindowState) -> dict:
    """
    For complex view requests that need design data (e.g., "zoom to net VDD"),
    query the C++ daemon for the necessary bounding box or layer information.

    If viewer_commands is already populated from parse_view_request, skip this node.
    """
    if state.get("viewer_commands") or state.get("error"):
        return {}  # Already resolved or errored — nothing to do

    req = state.get("view_request", "").lower()
    params = state.get("view_params", {})

    # "zoom to net <name>" → query net bbox from DB
    if "zoom" in req and "net" in req:
        net_name = params.get("net_name", "")
        if not net_name:
            # Try to extract from the request string  (e.g., "zoom to net VDD")
            parts = req.split()
            if "net" in parts:
                idx = parts.index("net")
                if idx + 1 < len(parts):
                    net_name = parts[idx + 1].upper()

        if net_name:
            try:
                result = await mcp_call("db.net_bbox", {"net_name": net_name})
                bbox = result.get("bbox", [])
                if bbox:
                    return {"viewer_commands": [
                        {"action": "zoom_to", "bbox": bbox},
                        {"action": "highlight", "net_name": net_name, "color": "#ffaa00"},
                    ]}
            except RuntimeError:
                pass  # Fall through to error

    return {"error": f"Could not resolve view request: '{state['view_request']}'"}


async def format_window_response(state: WindowState) -> dict:
    """
    Build the final AI message acknowledging what view action was taken.
    Simple factual description — no heavy LLM needed for most view actions.
    """
    if state.get("error"):
        return {"messages": [HumanMessage(content=f"View error: {state['error']}")]}

    cmds = state.get("viewer_commands", [])
    actions = [c.get("action", "?") for c in cmds]
    msg = f"View update sent to KLayout: {', '.join(actions)}."
    from langchain_core.messages import AIMessage
    return {"messages": [AIMessage(content=msg)]}


def create_window_subgraph() -> StateGraph:
    """Builds and compiles the Window Automation module subgraph (m4)."""
    wf = StateGraph(WindowState)
    wf.add_node("parse",   parse_view_request)
    wf.add_node("resolve", resolve_view_parameters)
    wf.add_node("format",  format_window_response)
    wf.add_edge(START,     "parse")
    wf.add_edge("parse",   "resolve")
    wf.add_edge("resolve", "format")
    wf.add_edge("format",  END)
    return wf.compile()
