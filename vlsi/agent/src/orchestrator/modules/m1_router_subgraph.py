"""
agent/modules/m1_router_subgraph.py  —  Module m1: Router Subgraph
═══════════════════════════════════════════════════════════════════════════════

WHAT THIS MODULE IS:
  The LangGraph subgraph for ALL routing operations.
  It is a compiled graph that Graph A (orchestrator) inserts as a single node.

INTERNAL FLOW:
  ┌──────────────┐    ┌─────────────────┐    ┌──────────────────────────┐
  │ validate_     │    │ call_router_     │    │ format_router_           │
  │ routing_input │───▶│ via_cli          │───▶│ response                 │
  │               │    │ (mcp_call)       │    │ (LLM narration)          │
  └──────────────┘    └─────────────────┘    └──────────────────────────┘

WHAT EACH NODE DOES:
  1. validate_routing_input:
       Checks that the design is loaded (calls "ping" then "db.check_loaded").
       If not loaded: returns an error message without wasting time on routing.

  2. call_router_via_cli:
       Calls mcp_call("route_nets", params) to the eda_cli daemon.
       The daemon forwards this to the C++ DetailedGridRouter → NRL → etc.
       Returns raw metrics: wirelength, via_count, drc_violations.

  3. format_router_response:
       Uses LLM to turn raw metrics JSON into a human-readable summary.
       Example: "Routing completed successfully!  Total wirelength: 82,540 µm.
                 3 DRC violations remain — consider running DRC Fix workflow."

WHY LLM IN format NODE ONLY?
  - validate and call_router are deterministic logic — no LLM needed.
  - Only the final natural-language narration uses the LLM.
  - This minimizes LLM call latency and cost.

COUPLING RULE:
  This subgraph NEVER imports from m2, m3, or m4.
  If routing needs DB information (e.g., net list), it asks Graph A which
  then queries m3 and passes the result INTO m1 through RouterState.
"""

import logging
from typing import TypedDict, Annotated, Sequence, Any

import operator
from langchain_core.messages import BaseMessage, AIMessage, HumanMessage
from langchain_google_genai import ChatGoogleGenerativeAI
from langgraph.graph import StateGraph, START, END

from ..utils.env_bootstrap import gemini_api_key
from ._cli_client import mcp_call, ping

logger = logging.getLogger(__name__)


# ─── State ────────────────────────────────────────────────────────────────────
class RouterState(TypedDict):
    """
    State flowing through the Router subgraph.

    messages:
        Accumulated conversation messages.  Graph A prepends the user request
        before invoking this subgraph.

    net_ids:
        Optional list of specific net IDs to route.  If empty, the router
        routes ALL nets in the loaded design.

    routing_params:
        Extra parameters passed to the C++ router:
        e.g. {"max_passes": 30, "strategy": "minimize_vias"}

    routing_result:
        Raw dictionary returned by the C++ daemon.
        Shape: {"status": str, "wirelength": float, "via_count": int,
                "drc_violations": int, "congestion_max": float}

    error:
        Non-empty string if validation or routing failed.
    """
    messages:       Annotated[Sequence[BaseMessage], operator.add]
    net_ids:        list[int]
    routing_params: dict[str, Any]
    routing_result: dict[str, Any]
    error:          str


# ─── Node 1: Validate ─────────────────────────────────────────────────────────
async def validate_routing_input(state: RouterState) -> dict:
    """
    WHAT IT DOES:
      Checks the C++ daemon is alive and a design is loaded.
      Returns an error string if either check fails so downstream nodes skip
      expensive computation.

    WHY VALIDATE FIRST?
      If the user asks "route the design" but no design is loaded, the C++
      daemon will return an error anyway — but we can give a MUCH better user
      message here ("No design loaded.  Use 'load_design' first.") instead of a
      cryptic daemon error.

    PING CHECK:
      Calls the "ping" method on the CLI client.  The daemon responds "pong".
      If the daemon is not running, ping() catches the ConnectionRefusedError.
    """
    logger.info("[Router] Validating: checking daemon alive...")

    daemon_alive = await ping()
    if not daemon_alive:
        return {"error": (
            "The C++ EDA daemon is not running.  "
            "Please start it: cd eda_cli && ./build/eda_daemon"
        )}

    try:
        db_status = await mcp_call("db.status")
        if not db_status.get("is_loaded", False):
            return {"error": (
                "No design is currently loaded in the EDA engine.  "
                "Ask me to 'load_design <filename.def>' first."
            )}
    except RuntimeError as e:
        return {"error": f"Daemon status check failed: {e}"}

    return {"error": ""}  # "" = no error = proceed


# ─── Node 2: Call Router ──────────────────────────────────────────────────────
async def call_router_via_cli(state: RouterState) -> dict:
    """
    WHAT IT DOES:
      Sends the "route_nets" JSON-RPC request to the C++ eda_cli daemon.

    PARAMETERS PASSED (from RouterState.routing_params):
      net_ids     (optional) — route only these nets; [] = route all
      max_passes  (optional) — NRL iteration limit (default: 30)
      strategy    (optional) — "minimize_vias" | "minimize_wirelength" | "auto"

    WHAT HAPPENS IN THE C++ DAEMON:
      1. eda_daemon.cpp receives "route_nets"
      2. Dispatches to RoutingMcpServer::route_nets()
      3. RoutingMcpServer creates RoutingPipelineOrchestrator
      4. Runs: GlobalPlanner (GA) → NegotiatedRoutingLoop → RouteEvaluator
      5. Returns RouteEvaluation metrics as JSON

    IF ERROR OCCURS (state.error is non-empty):
      Skip the MCP call entirely and return unchanged state.
      The format node will compose an error response for the user.
    """
    if state.get("error"):
        logger.warning("[Router] Skipping MCP call: %s", state["error"])
        return {}  # Nothing to update; error propagates

    params = {
        "net_ids":    state.get("net_ids", []),
        **state.get("routing_params", {}),
    }

    logger.info("[Router] Calling eda_cli route_nets with params: %s", params)

    try:
        result = await mcp_call("route_nets", params)
        return {"routing_result": result}
    except RuntimeError as e:
        return {"error": str(e), "routing_result": {}}


# ─── Node 3: Format Response ──────────────────────────────────────────────────
async def format_router_response(state: RouterState) -> dict:
    """
    WHAT IT DOES:
      Converts raw C++ metrics (or an error string) into a natural-language
      message the user can understand.

    USES LLM FOR:
      - Narrating metric numbers ("82540 µm" → "about 82.5 mm of wire")
      - Explaining DRC violations ("3 violations: via enclosure on M2")
      - Suggesting next steps ("Run DRC Fix workflow to resolve remaining issues")

    WHY NOT HARD-CODE THE FORMATTING?
      EDA metrics are context-dependent.  A DRC violation in a clock tree
      is very different from one in a power rail.  The LLM can read the
      design context from the conversation history and tailor its response.
    """
    llm = ChatGoogleGenerativeAI(
        model="gemini-2.0-flash",
        temperature=0.3,
        api_key=gemini_api_key(),
    )

    if state.get("error"):
        # Error path: ask LLM to compose a friendly error explanation.
        prompt = (
            f"The user asked to route a VLSI design but encountered this problem:\n"
            f"{state['error']}\n\n"
            f"Explain the problem in plain English and tell the user what they should do next."
        )
        ai_resp = llm.invoke([HumanMessage(content=prompt)])
        return {"messages": [ai_resp]}

    # Success path: narrate the routing metrics.
    result = state.get("routing_result", {})
    prompt = (
        f"VLSI routing completed.  Here are the technical metrics:\n"
        f"  Wirelength:      {result.get('wirelength', 'N/A')} µm\n"
        f"  Via count:       {result.get('via_count', 'N/A')}\n"
        f"  DRC violations:  {result.get('drc_violations', 'N/A')}\n"
        f"  Congestion max:  {result.get('congestion_max', 'N/A')}\n\n"
        f"Summarize these results for a chip designer who may not know all the jargon. "
        f"Suggest follow-up actions if DRC violations > 0.  "
        f"Keep the summary to 3-4 sentences."
    )
    ai_resp = llm.invoke([HumanMessage(content=prompt)])
    return {"messages": [ai_resp]}


# ─── Subgraph Factory ─────────────────────────────────────────────────────────
def create_router_subgraph() -> StateGraph:
    """
    Builds and compiles the Router module subgraph (m1).

    Graph A inserts the compiled result as a single node called "m1_router".
    From Graph A's perspective, m1_router is a black box that takes RouterState
    and returns updated RouterState — the internal three-node flow is hidden.

    FLOW:
      START → validate_routing_input → call_router_via_cli → format_router_response → END
    """
    wf = StateGraph(RouterState)

    wf.add_node("validate",  validate_routing_input)
    wf.add_node("call_cli",  call_router_via_cli)
    wf.add_node("format",    format_router_response)

    wf.add_edge(START,      "validate")
    wf.add_edge("validate", "call_cli")
    wf.add_edge("call_cli", "format")
    wf.add_edge("format",   END)

    return wf.compile()
