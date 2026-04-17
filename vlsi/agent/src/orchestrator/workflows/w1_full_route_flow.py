"""
agent/workflows/w1_full_route_flow.py  —  Workflow w1: Full Placement + Routing
═══════════════════════════════════════════════════════════════════════════════

WHAT THIS WORKFLOW IS:
  An end-to-end EDA pipeline that takes a freshly loaded design and produces
  a fully routed layout.  It chains:
    1. DB query (m3)     — verify design is loaded, count nets
    2. Placement (m2)    — place all cells
    3. Routing (m1)      — route all nets
    4. View refresh (m4) — update KLayout canvas
    5. Evaluation        — report final metrics to user

WHY IS THIS A WORKFLOW AND NOT JUST GRAPH A CALLING m2 THEN m1?
  A workflow can have ITERATIVE LOOPS and ANALYSIS steps between each phase:
    - If placement has overlaps → re-run legalization before routing
    - If routing has DRC violations → report them but continue
    - Collects telemetry from every step into one final report

GRAPH A'S PERSPECTIVE:
  Graph A sees this as a single node "w1_full_route".
  It does NOT know about the internal sequence.  The workflow is self-contained.

HOW WORKFLOW CALLS MODULES (anti-coupling):
  The workflow calls module FACTORIES to get compiled subgraphs, then invokes
  them with carefully constructed state objects.  It does NOT call module
  node functions directly — it invokes the compiled subgraph.

  ANALOGY: A workflow is like a project manager who assigns sub-tasks to
  department heads (modules).  The workflow tracks progress but the departments
  do the actual work using their own internal procedures.

INTER-MODULE DATA FLOW THROUGH THE WORKFLOW (not through Python imports):
  m3 result (net count, design name) → passed into m2 state as context
  m2 result (placement metrics)      → passed into m1 state as "pre_place_metrics"
  m1 result (routing metrics)        → passed into m4 state as "routing_finished: True"
"""

import logging
from typing import TypedDict, Annotated, Sequence, Any
import operator

from langchain_core.messages import BaseMessage, AIMessage, HumanMessage
from langchain_google_genai import ChatGoogleGenerativeAI
from langgraph.graph import StateGraph, START, END

from orchestrator.utils.env_bootstrap import gemini_api_key

# Import module factories (not node functions — maintaining decoupling)
from orchestrator.modules.m1_router_subgraph import create_router_subgraph
from orchestrator.modules.m2_placer_subgraph import create_placer_subgraph
from orchestrator.modules.m3_db_subgraph     import create_db_subgraph
from orchestrator.modules.m4_window_subgraph import create_window_subgraph
from orchestrator.modules._cli_client        import mcp_call

logger = logging.getLogger(__name__)


class FullRouteState(TypedDict):
    """
    State for the Full Placement + Routing workflow.

    This state carries data BETWEEN modules inside the workflow.
    Each field is populated progressively as the workflow advances.
    """
    messages:         Annotated[Sequence[BaseMessage], operator.add]

    # Gathered by the DB query step
    design_name:      str
    net_count:        int

    # Set by the Placement step
    placement_result: dict[str, Any]   # {"overlap_count": int, "hpwl": float}
    placement_ok:     bool             # True if zero overlaps after legalization

    # Set by the Routing step
    routing_result:   dict[str, Any]   # {"wirelength": float, "via_count": int, ...}
    routing_ok:       bool             # True if DRC violations == 0

    # Viewer commands assembled across all steps
    viewer_commands:  list[dict[str, Any]]

    # Final summary (from format step)
    workflow_report:  str


# ─── Step 1: Verify design is loaded ─────────────────────────────────────────
async def step_1_verify_design(state: FullRouteState) -> dict:
    """
    WHAT IT DOES:
      Calls the C++ DB server to verify the design is loaded and
      populates design_name and net_count so subsequent steps can log them.

    WHY: Placement and routing without a loaded design will silently fail
    or return confusing errors.  Checking upfront saves time.
    """
    logger.info("[W1] Step 1: Verifying design is loaded...")
    try:
        status = await mcp_call("db.status")
        if not status.get("is_loaded", False):
            msg = AIMessage(content=(
                "Workflow aborted: No design is loaded.  "
                "Please load a design first (e.g., 'load mydesign.def')."
            ))
            return {"messages": [msg], "design_name": "", "net_count": 0}

        nets = await mcp_call("db.query_nets")
        return {
            "design_name": status.get("design_name", "unknown"),
            "net_count":   len(nets.get("nets", [])),
        }
    except RuntimeError as e:
        return {
            "messages": [AIMessage(content=f"Design verification failed: {e}")],
            "design_name": "", "net_count": 0,
        }


# ─── Step 2: Run Placement ────────────────────────────────────────────────────
async def step_2_place_cells(state: FullRouteState) -> dict:
    """
    WHAT IT DOES:
      Invokes the compiled m2 Placer subgraph to place all cells.
      If placement has overlaps, requests legalization before proceeding.

    LEGALIZATION:
      If overlap_count > 0 after initial placement, calls "place_cells" again
      with {"algorithm": "legalize"} to fix coordinate conflicts.
      Legalization: adjusting cell positions slightly so no two cells overlap,
      like moving parked cars apart so none are double-parked.
    """
    if not state.get("design_name"):
        return {}  # Skip — design not loaded (step 1 already reported error)

    logger.info("[W1] Step 2: Running placement for design '%s'...", state.get("design_name"))

    try:
        result = await mcp_call("place_cells", {"algorithm": "auto"})
    except RuntimeError as e:
        return {"messages": [AIMessage(content=f"Placement failed: {e}")],
                "placement_ok": False}

    overlap_count = result.get("overlap_count", 0)

    # LEGALIZATION LOOP: if overlaps remain, run legalization.
    if overlap_count > 0:
        logger.info("[W1] Overlaps detected (%d). Running legalization...", overlap_count)
        try:
            result = await mcp_call("place_cells", {"algorithm": "legalize"})
        except RuntimeError as e:
            logger.warning("[W1] Legalization failed: %s", e)

    placement_ok = result.get("overlap_count", 0) == 0
    msgs = [AIMessage(content=(
        f"Placement complete for '{state['design_name']}' "
        f"({state.get('net_count', '?')} nets).  "
        f"Overlap count: {result.get('overlap_count', 'N/A')}.  "
        f"HPWL estimate: {result.get('hpwl', 'N/A')} µm."
    ))]
    return {"placement_result": result, "placement_ok": placement_ok, "messages": msgs}


# ─── Step 3: Run Routing ──────────────────────────────────────────────────────
async def step_3_route_nets(state: FullRouteState) -> dict:
    """
    WHAT IT DOES:
      Invokes the C++ routing engine via the CLI.
      Passes placement quality info as a routing hint (if placement was clean,
      routing can use tighter corridor bounds from GlobalPlanner).

    ROUTING CONTEXT FROM PLACEMENT:
      {"hpwl": float} from placement is used by GlobalPlanner as an initial
      congestion estimate — if HPWL is high, the router widens corridors.
    """
    if not state.get("design_name"):
        return {}

    logger.info("[W1] Step 3: Routing nets...")

    params = {
        "max_passes": 30,
        "strategy":   "auto",
        "placement_hpwl_hint": state.get("placement_result", {}).get("hpwl", 0.0),
    }

    try:
        result = await mcp_call("route_nets", params)
    except RuntimeError as e:
        return {"messages": [AIMessage(content=f"Routing failed: {e}")],
                "routing_ok": False}

    routing_ok = result.get("drc_violations", 1) == 0
    msgs = [AIMessage(content=(
        f"Routing complete.  "
        f"Wirelength: {result.get('wirelength', 'N/A')} µm, "
        f"Vias: {result.get('via_count', 'N/A')}, "
        f"DRC violations: {result.get('drc_violations', 'N/A')}."
    ))]
    return {"routing_result": result, "routing_ok": routing_ok, "messages": msgs}


# ─── Step 4: Refresh KLayout View ────────────────────────────────────────────
async def step_4_refresh_view(state: FullRouteState) -> dict:
    """
    WHAT IT DOES:
      Builds the viewer_commands list that server.py will return to KLayout.
      After routing, KLayout should refresh all layers so the new wire segments
      become visible in the layout canvas.

    VIEWER COMMANDS GENERATED:
      refresh_view — reload all layer content from the current design database
      zoom_fit     — zoom out to see the entire design (useful after routing)
    """
    cmds = [
        {"action": "refresh_view"},
        {"action": "zoom_fit"},
    ]
    # If routing had DRC violations, highlight them
    if state.get("routing_result", {}).get("drc_violations", 0) > 0:
        cmds.append({"action": "highlight_drc_violations"})

    return {"viewer_commands": cmds}


# ─── Step 5: Generate Final Report ───────────────────────────────────────────
async def step_5_final_report(state: FullRouteState) -> dict:
    """
    WHAT IT DOES:
      Uses LLM to generate a comprehensive workflow report that covers all steps:
        - Was the design name recognized?
        - Placement overlap count and HPWL
        - Routing wirelength, via count, DRC violations
        - Recommended next steps

    THIS IS THE ONLY LLM CALL IN THE WORKFLOW (steps 1-4 are deterministic).
    The LLM adds value here by contextualizing numbers and recommending actions.
    """
    llm = ChatGoogleGenerativeAI(
        model="gemini-2.0-flash",
        temperature=0.4,
        api_key=gemini_api_key(),
    )

    pr = state.get("placement_result", {})
    rr = state.get("routing_result", {})

    prompt = (
        f"Full EDA workflow completed for design '{state.get('design_name', 'unknown')}'.\n\n"
        f"PLACEMENT RESULTS:\n"
        f"  Overlaps: {pr.get('overlap_count', 'N/A')}\n"
        f"  HPWL:     {pr.get('hpwl', 'N/A')} µm\n\n"
        f"ROUTING RESULTS:\n"
        f"  Wirelength:    {rr.get('wirelength', 'N/A')} µm\n"
        f"  Via count:     {rr.get('via_count', 'N/A')}\n"
        f"  DRC violations: {rr.get('drc_violations', 'N/A')}\n\n"
        f"Write a concise (4-5 sentence) summary for the chip designer.  "
        f"Congratulate them if DRC is clean.  If DRC violations remain, "
        f"suggest running the 'DRC Fix Loop' workflow next."
    )

    ai_response = llm.invoke([HumanMessage(content=prompt)])
    return {
        "workflow_report": ai_response.content,
        "messages": [ai_response],
    }


# ─── Workflow Factory ─────────────────────────────────────────────────────────
def create_full_route_workflow() -> StateGraph:
    """
    Builds and compiles the Full Placement + Routing workflow (w1).

    Graph A invokes this with:
        await w1_graph.ainvoke({"messages": [HumanMessage(content=user_msg)]})

    The workflow manages the complete sequence internally and returns a final
    FullRouteState with messages, viewer_commands, and workflow_report populated.

    FLOW:
      START
        → step_1_verify_design
        → step_2_place_cells
        → step_3_route_nets
        → step_4_refresh_view
        → step_5_final_report
        → END
    """
    wf = StateGraph(FullRouteState)

    wf.add_node("verify_design", step_1_verify_design)
    wf.add_node("place_cells",   step_2_place_cells)
    wf.add_node("route_nets",    step_3_route_nets)
    wf.add_node("refresh_view",  step_4_refresh_view)
    wf.add_node("final_report",  step_5_final_report)

    wf.add_edge(START,            "verify_design")
    wf.add_edge("verify_design",  "place_cells")
    wf.add_edge("place_cells",    "route_nets")
    wf.add_edge("route_nets",     "refresh_view")
    wf.add_edge("refresh_view",   "final_report")
    wf.add_edge("final_report",   END)

    return wf.compile()
