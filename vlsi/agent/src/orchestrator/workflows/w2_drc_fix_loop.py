"""
agent/workflows/w2_drc_fix_loop.py  —  Workflow w2: Iterative DRC Analysis + Fix
═══════════════════════════════════════════════════════════════════════════════

WHAT THIS WORKFLOW IS:
  An iterative analysis-fix loop that:
    1. Queries current DRC violations from the router
    2. If violations exist: invokes the ECO router to fix the specific violations
    3. Re-checks DRC
    4. Repeats up to MAX_ITERATIONS times
    5. Reports final DRC status

WHAT IS ECO ROUTING?
  ECO = Engineering Change Order.  In chip design, an ECO is a small targeted
  change to fix a specific problem WITHOUT re-routing the whole design.
  The C++ EcoRouter (eco_router.hpp) freezes all clean routes and only re-
  routes the specific nets that have DRC violations.

  ANALOGY: Instead of repaving an entire city after one pothole report,
  you send one repair crew to fix just the broken section.

ITERATION CONTROL:
  max_iterations = 5 (default) — prevents infinite loops if DRC cannot be resolved.
  If violations stay the same across 2 consecutive passes → "stuck" → exit early.

LANGGRAPH CONDITIONAL EDGE:
  After each "check_drc" node, a conditional edge decides:
    - drc_clean OR iterations_exhausted → go to "final_report"
    - violations_remain               → go to "eco_fix"
  Then eco_fix → check_drc creates the ITERATION LOOP inside LangGraph.

  LANGGRAPH LOOPS:
    Unlike simple linear graphs, LangGraph allows a node to point BACK to
    a previous node.  The iteration counter in DrcFixState prevents infinite loops.

COUPLING RULE:
  w2 calls the C++ daemon directly for DRC check and ECO fix.
  It does NOT import m1 or any other module — it goes straight to CLI for speed.
"""

import logging
from typing import TypedDict, Annotated, Sequence, Any
import operator

from langchain_core.messages import BaseMessage, AIMessage, HumanMessage
from langchain_google_genai import ChatGoogleGenerativeAI
from langgraph.graph import StateGraph, START, END

from orchestrator.modules._cli_client import mcp_call
from orchestrator.utils.env_bootstrap import gemini_api_key

logger = logging.getLogger(__name__)

MAX_ITERATIONS = 5


class DrcFixState(TypedDict):
    """
    State for the DRC Fix Loop workflow.

    iteration:        Current loop count (starts at 0, max MAX_ITERATIONS)
    drc_violations:   List of violation descriptions from the DRC checker
    violation_count:  Total violations (shortcut for conditional edge check)
    prev_count:       Violations in previous pass (used to detect "stuck")
    fix_result:       ECO router result for the most recent fix pass
    messages:         Accumulated conversation messages
    viewer_commands:  KLayout viewer commands (highlight DRC errors)
    """
    messages:        Annotated[Sequence[BaseMessage], operator.add]
    iteration:       int
    drc_violations:  list[dict[str, Any]]
    violation_count: int
    prev_count:      int
    fix_result:      dict[str, Any]
    viewer_commands: list[dict[str, Any]]


# ─── Node: Check DRC ──────────────────────────────────────────────────────────
async def check_drc(state: DrcFixState) -> dict:
    """
    WHAT IT DOES:
      Queries the C++ daemon for the current list of DRC violations.
      Updates violation_count and prev_count for the conditional edge.

    C++ METHOD CALLED: "drc.check"
    RETURNS: {"violations": [{"type": "via_enclosure", "net": "VDD", "location": [x,y,l]}, ...]}

    EACH VIOLATION HAS:
      type:      rule name (e.g., "via_enclosure", "min_width", "min_spacing")
      net:       which net is violating
      location:  [x, y, layer] on the routing grid
      severity:  "error" | "warning"
    """
    logger.info("[W2] Checking DRC (iteration %d)...", state.get("iteration", 0))

    try:
        result = await mcp_call("drc.check", {})
        violations = result.get("violations", [])
        count = len(violations)
    except RuntimeError as e:
        logger.error("[W2] DRC check failed: %s", e)
        violations = []
        count      = -1  # Sentinel: means check failed

    prev_count = state.get("violation_count", count + 1)

    msgs = [AIMessage(content=(
        f"DRC check (pass {state.get('iteration', 0) + 1}): "
        f"{count} violation(s) found."
    ))]

    # Highlight DRC violations in KLayout
    cmds: list[dict] = []
    if count > 0:
        cmds = [{"action": "highlight_drc_violations", "violations": violations[:10]}]
        # Show at most 10 in the viewer to avoid performance issues

    return {
        "drc_violations":  violations,
        "violation_count": count,
        "prev_count":      prev_count,
        "messages":        msgs,
        "viewer_commands": cmds,
        "iteration":       state.get("iteration", 0) + 1,
    }


# ─── Node: ECO Fix ────────────────────────────────────────────────────────────
async def eco_fix(state: DrcFixState) -> dict:
    """
    WHAT IT DOES:
      Sends the current DRC violations list to the C++ EcoRouter.
      The EcoRouter (eco_router.hpp) freezes all clean nets and re-routes
      only the violating nets.

    C++ METHOD CALLED: "eco.fix_drc"
    PARAMETERS:
      violations: list of violation dicts from check_drc

    WHY PASS THE VIOLATION LIST?
      The ECO router uses the violation locations to determine which net
      segments to rip up and re-route.  Passing the same list the checker
      produced is the exact set it needs to repair.
    """
    logger.info("[W2] Running ECO fix for %d violations...", state.get("violation_count", 0))

    try:
        result = await mcp_call("eco.fix_drc", {
            "violations": state.get("drc_violations", [])
        })
        msgs = [AIMessage(content=(
            f"ECO fix applied.  "
            f"Re-routed {result.get('nets_rerouted', '?')} nets.  "
            f"Re-checking DRC..."
        ))]
        return {"fix_result": result, "messages": msgs}
    except RuntimeError as e:
        return {
            "fix_result": {},
            "messages": [AIMessage(content=f"ECO fix failed: {e}  Stopping loop.")],
            "violation_count": 0,  # Force exit from loop on ECO failure
        }


# ─── Node: Final Report ───────────────────────────────────────────────────────
async def drc_final_report(state: DrcFixState) -> dict:
    """
    WHAT IT DOES:
      Summarises the entire DRC fix session using LLM.
      Reports: total iterations, remaining violations, recommended next steps.
    """
    llm = ChatGoogleGenerativeAI(
        model="gemini-2.0-flash",
        temperature=0.3,
        api_key=gemini_api_key(),
    )
    count = state.get("violation_count", -1)
    iters = state.get("iteration", 0)

    if count == 0:
        outcome = "All DRC violations have been resolved. The design is DRC-clean!"
    elif count < 0:
        outcome = "DRC check could not be completed (daemon error)."
    else:
        outcome = (
            f"{count} DRC violation(s) remain after {iters} ECO fix iteration(s).  "
            f"The routing engine could not automatically resolve these.  "
            f"Manual intervention may be required."
        )

    prompt = (
        f"VLSI DRC Fix Loop Summary:\n"
        f"  Iterations run:       {iters}\n"
        f"  Final violation count: {count}\n"
        f"  Outcome: {outcome}\n\n"
        f"Write a 3-4 sentence summary for the chip designer with clear next steps."
    )
    ai_resp = llm.invoke([HumanMessage(content=prompt)])
    return {"messages": [ai_resp]}


# ─── Conditional Edge: should we keep looping? ───────────────────────────────
def should_continue_fixing(state: DrcFixState) -> str:
    """
    WHAT IT DOES:
      Decides whether to loop back to eco_fix or proceed to final_report.

    CONDITIONS TO STOP FIXING (go to "final_report"):
      1. violation_count == 0           → clean, done!
      2. iteration >= MAX_ITERATIONS    → exceeded retry limit
      3. violation_count == prev_count  → stuck, same count twice in a row
      4. violation_count < 0            → DRC check failed

    ANALOGY: A medical doctor reviewing lab tests.
      If the patient is healthy → discharge (final_report).
      If the patient hasn't improved in 5 visits → escalate (final_report).
      If the patient improved → continue treatment (eco_fix).

    IN LANGGRAPH:
      This function is the "routing function" for a conditional edge:
        add_conditional_edges("check_drc", should_continue_fixing,
            {"eco_fix": "eco_fix", "final_report": "final_report"})
    """
    count = state.get("violation_count", 0)
    iters = state.get("iteration", 0)
    prev  = state.get("prev_count", count + 1)

    if count <= 0:
        return "final_report"
    if iters >= MAX_ITERATIONS:
        logger.info("[W2] Reached max iterations (%d). Stopping.", MAX_ITERATIONS)
        return "final_report"
    if count == prev:
        logger.info("[W2] No improvement (count=%d). Stopping.", count)
        return "final_report"

    return "eco_fix"


# ─── Workflow Factory ─────────────────────────────────────────────────────────
def create_drc_fix_workflow() -> StateGraph:
    """
    Builds and compiles the DRC Fix Loop workflow (w2).

    GRAPH STRUCTURE (with loop):
      START
        → check_drc
        → [conditional: should_continue_fixing]
              "eco_fix"      → eco_fix → check_drc  (LOOP BACK)
              "final_report" → final_report → END
    """
    wf = StateGraph(DrcFixState)

    wf.add_node("check_drc",    check_drc)
    wf.add_node("eco_fix",      eco_fix)
    wf.add_node("final_report", drc_final_report)

    wf.add_edge(START,       "check_drc")

    # CONDITIONAL EDGE: after check_drc, decide loop-or-stop
    wf.add_conditional_edges(
        "check_drc",
        should_continue_fixing,
        {
            "eco_fix":      "eco_fix",
            "final_report": "final_report",
        }
    )

    wf.add_edge("eco_fix",      "check_drc")   # ← THIS IS THE LOOP BACK EDGE
    wf.add_edge("final_report", END)

    return wf.compile()
