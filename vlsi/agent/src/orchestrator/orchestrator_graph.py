"""
agent/orchestrator_graph.py  —  Graph A: The Master Orchestrator
═══════════════════════════════════════════════════════════════════════════════

WHAT THIS FILE IS:
  The central LangGraph orchestrator that handles every user request from
  KLayout.  Think of it as the "General" that receives orders and decides:
    - Which module (m1, m2, m3, m4) to call directly for a simple task
    - Which workflow (w1, w2) to call for a multi-step pipeline

HOW GRAPH A RELATES TO MODULES AND WORKFLOWS:
  ┌──────────────────────────────────────────────────────────────┐
  │  Graph A  (this file)                                        │
  │                                                              │
  │  parse_intent  (LLM)                                         │
  │       │                                                      │
  │       ├─── intent == "route"      → m1 Router subgraph       │
  │       ├─── intent == "place"      → m2 Placer subgraph       │
  │       ├─── intent == "db_query"   → m3 DB subgraph           │
  │       ├─── intent == "view"       → m4 Window subgraph       │
  │       ├─── intent == "full_route" → w1 workflow              │
  │       ├─── intent == "fix_drc"    → w2 workflow              │
  │       └─── intent == "chat"       → chat_node (LLM reply)    │
  │                                                              │
  │  merge_outputs (collect messages + viewer_commands from any  │
  │  module or workflow and format a unified ChatResponse)       │
  └──────────────────────────────────────────────────────────────┘

MASTER STATE vs MODULE STATES:
  OrchestratorState is the master state.
  Each module/workflow has its OWN state type (RouterState, PlacerState, etc.)
  When Graph A calls a subgraph, it maps OrchestratorState fields INTO the
  subgraph's input state.  When the subgraph finishes, Graph A maps the
  output back into OrchestratorState.

INTENT RECOGNITION (parse_intent node):
  Uses a fast LLM call with a structured prompt to extract:
    intent:   one of the 7 intents above
    params:   relevant parameters (e.g., net names, filenames, algorithms)
  The parsing is LLM-powered so natural language like "please route my design
  using fewer vias" correctly maps to intent="route" with
  params={"strategy": "minimize_vias"}.

ANTI-COUPLING GUARANTEE:
  orchestrator_graph.py imports module FACTORIES (not node functions).
  Modules never import each other.  Only orchestrator_graph imports from
  both modules/ and workflows/.
"""

import os
import json
import logging
from typing import TypedDict, Annotated, Sequence, Any
import operator

from .utils.env_bootstrap import gemini_api_key, load_agent_env

load_agent_env()

from langchain_core.messages import BaseMessage, HumanMessage, AIMessage
from langchain_google_genai import ChatGoogleGenerativeAI
from langgraph.graph import StateGraph, START, END

# Module subgraph factories (m1..m4)
from .modules import (
    create_router_subgraph,
    create_placer_subgraph,
    create_db_subgraph,
    create_window_subgraph,
)
# Workflow factories (w1, w2)
from .workflows import (
    create_full_route_workflow,
    create_drc_fix_workflow,
)

logger = logging.getLogger(__name__)

# ─── Master State ─────────────────────────────────────────────────────────────
class OrchestratorState(TypedDict):
    """
    The shared state that flows through Graph A.

    messages:
        The complete conversation history (user + AI messages).
        Uses operator.add so each node APPENDS rather than REPLACES.

    active_intent:
        Which capability was identified:
        "route" | "place" | "db_query" | "view" | "full_route" | "fix_drc" | "chat"

    intent_params:
        Structured parameters extracted by parse_intent from the user message.
        Passed into the appropriate module/workflow state.

    viewer_commands:
        KLayout viewer commands collected from any module that produces them.
        Merged at collect_outputs and returned to the KLayout macro via server.py.

    layout_locked:
        Flag set by KLayout when it sends a request (UI is locked).
        Cleared by "unlock_ui" viewer command after response.
    """
    messages:       Annotated[Sequence[BaseMessage], operator.add]
    active_intent:  str
    intent_params:  dict[str, Any]
    viewer_commands: list[dict[str, Any]]
    layout_locked:  bool


# ─── Node: parse_intent ───────────────────────────────────────────────────────
async def parse_intent(state: OrchestratorState) -> dict:
    """
    WHAT IT DOES:
      Reads the last user message and uses a fast LLM call to extract:
        - active_intent: which module/workflow to invoke
        - intent_params: structured parameters extracted from natural language

    WHY LLM FOR INTENT PARSING?
      EDA users phrase requests in many ways:
        "route the power rail"   → intent=route, params={"net_name": "VDD"}
        "run full chip flow"     → intent=full_route
        "how many vias are there?" → intent=db_query, params={"query": "via_count"}
      Keyword matching (the old approach) misses too many phrasings.
      A small LLM call (gemini-flash) is fast (~0.3s) and much more robust.

    OUTPUT FORMAT (enforced by the prompt):
      {"intent": "route", "params": {"strategy": "minimize_vias", "net_ids": []}}

    FALLBACK:
      If the LLM returns invalid JSON, parse_intent falls back to "chat" intent
      so the user at least gets a readable response instead of an error.
    """
    llm = ChatGoogleGenerativeAI(
        model="gemini-2.0-flash",
        temperature=0,
        api_key=gemini_api_key(),
    )

    last_msg = state["messages"][-1].content if state["messages"] else ""

    system_prompt = """You are an intent router for a VLSI EDA AI agent.
Extract the user's intent and parameters from their message.

Valid intents:
  route      - route nets or wires
  place      - place standard cells or macros
  db_query   - query design database (net counts, cell info, etc.)
  view       - KLayout view operations (zoom, refresh, highlight)
  full_route - run complete placement + routing workflow
  fix_drc    - run DRC analysis and iterative ECO fix loop
  chat       - general question, no EDA tool action needed

Respond ONLY with valid JSON, no explanation:
{"intent": "<intent>", "params": {<relevant key-value pairs from the message>}}
"""

    try:
        response  = llm.invoke([
            HumanMessage(content=system_prompt),
            HumanMessage(content=f"User message: {last_msg}"),
        ])
        parsed    = json.loads(response.content.strip())
        intent    = parsed.get("intent", "chat")
        params    = parsed.get("params", {})
    except (json.JSONDecodeError, Exception) as e:
        logger.warning("Intent parsing failed (%s), defaulting to 'chat'", e)
        intent = "chat"
        params = {}

    logger.info("[Graph A] Intent: %s  Params: %s", intent, params)
    return {"active_intent": intent, "intent_params": params}


# ─── Node: chat_node ──────────────────────────────────────────────────────────
async def chat_node(state: OrchestratorState) -> dict:
    """
    WHAT IT DOES:
      Handles general questions that do NOT require EDA tools.
      Examples: "What is a Steiner tree?", "Explain DRC rules", "Hello!"

    WHY NOT ROUTE EVERYTHING THROUGH TOOLS?
      Some questions are purely informational.  Spinning up the C++ daemon
      for "what is via enclosure?" would be wasteful.  The LLM can answer
      directly from its knowledge.
    """
    llm = ChatGoogleGenerativeAI(
        model="gemini-2.0-flash",
        temperature=0.7,
        api_key=gemini_api_key(),
    )

    # Add system context so the LLM stays in VLSI domain
    system_msg = HumanMessage(content=(
        "You are an expert VLSI chip design assistant embedded in KLayout EDA tool.  "
        "Answer the user's question helpfully.  If they ask about chip routing, "
        "DRC, placement, or EDA workflows, give precise technical answers.  "
        "Keep responses under 5 sentences unless asked for more detail."
    ))

    # Prepend system context, then conversation history
    response = llm.invoke([system_msg] + list(state["messages"]))
    return {"messages": [response], "viewer_commands": []}


# ─── Node: collect_outputs ────────────────────────────────────────────────────
async def collect_outputs(state: OrchestratorState) -> dict:
    """
    WHAT IT DOES:
      Final cleanup node that always runs before END.
      - Ensures viewer_commands includes "unlock_ui" so KLayout re-enables input
      - Adds viewer_commands from state if a module/workflow added them
    """
    cmds = list(state.get("viewer_commands", []))

    # Always unlock the KLayout UI when the response is ready
    if state.get("layout_locked", False):
        cmds.append({"action": "unlock_ui"})

    return {"viewer_commands": cmds, "layout_locked": False}


# ─── Routing Function: which module/workflow to call ─────────────────────────
def dispatch_intent(state: OrchestratorState) -> str:
    """
    WHAT IT DOES:
      Returns the name of the NEXT node to execute based on active_intent.
      This is a LangGraph "routing function" — it determines CONDITIONAL EDGES.

    NODE NAME → WHAT IT DOES:
      "m1_router"    → Router subgraph (m1)
      "m2_placer"    → Placer subgraph (m2)
      "m3_db"        → DB subgraph (m3)
      "m4_window"    → Window subgraph (m4)
      "w1_full_route"→ Full Placement+Route workflow (w1)
      "w2_drc_fix"   → DRC Fix Loop workflow (w2)
      "chat_node"    → Fallback chat handler
    """
    intent_to_node = {
        "route":      "m1_router",
        "place":      "m2_placer",
        "db_query":   "m3_db",
        "view":       "m4_window",
        "full_route": "w1_full_route",
        "fix_drc":    "w2_drc_fix",
        "chat":       "chat_node",
    }
    return intent_to_node.get(state["active_intent"], "chat_node")


# ─── Graph Factory ────────────────────────────────────────────────────────────
def create_orchestrator_graph():
    """
    Builds and compiles Graph A — the master orchestrator.

    CALLED ONCE at server startup.  The compiled graph is reused for every
    chat request, which is much faster than recompiling per request.

    NODE LIST:
      intent_parser    — LLM intent extraction
      m1_router        — Router module subgraph  (compiled)
      m2_placer        — Placer module subgraph  (compiled)
      m3_db            — DB reader subgraph      (compiled)
      m4_window        — Window automation subgraph (compiled)
      w1_full_route    — Full Placement+Route workflow (compiled)
      w2_drc_fix       — DRC Fix Loop workflow (compiled)
      chat_node        — Fallback LLM chat handler
      collect_outputs  — Cleanup + unlock_ui

    EDGE STRUCTURE:
      START → intent_parser
      intent_parser → [conditional: dispatch_intent] → m1/m2/m3/m4/w1/w2/chat
      all module/workflow nodes → collect_outputs
      collect_outputs → END
    """
    wf = StateGraph(OrchestratorState)

    # ── Core nodes ────────────────────────────────────────────────────────────
    wf.add_node("intent_parser",   parse_intent)
    wf.add_node("chat_node",       chat_node)
    wf.add_node("collect_outputs", collect_outputs)

    # ── Module subgraphs (compiled once, reused per request) ──────────────────
    wf.add_node("m1_router",       create_router_subgraph())
    wf.add_node("m2_placer",       create_placer_subgraph())
    wf.add_node("m3_db",           create_db_subgraph())
    wf.add_node("m4_window",       create_window_subgraph())

    # ── Workflow subgraphs ────────────────────────────────────────────────────
    wf.add_node("w1_full_route",   create_full_route_workflow())
    wf.add_node("w2_drc_fix",      create_drc_fix_workflow())

    # ── Entry ────────────────────────────────────────────────────────────────
    wf.add_edge(START, "intent_parser")

    # ── Conditional dispatch from intent_parser ───────────────────────────────
    wf.add_conditional_edges(
        "intent_parser",
        dispatch_intent,
        {
            "m1_router":      "m1_router",
            "m2_placer":      "m2_placer",
            "m3_db":          "m3_db",
            "m4_window":      "m4_window",
            "w1_full_route":  "w1_full_route",
            "w2_drc_fix":     "w2_drc_fix",
            "chat_node":      "chat_node",
        }
    )

    # ── All paths converge at collect_outputs ─────────────────────────────────
    for node in ["m1_router", "m2_placer", "m3_db", "m4_window",
                 "w1_full_route", "w2_drc_fix", "chat_node"]:
        wf.add_edge(node, "collect_outputs")

    wf.add_edge("collect_outputs", END)

    return wf.compile()
