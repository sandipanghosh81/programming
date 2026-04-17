"""
agent/modules/m3_db_subgraph.py  —  Module m3: Database Reader Subgraph
═══════════════════════════════════════════════════════════════════════════════

WHAT THIS MODULE IS:
  The LangGraph subgraph for querying the EDA design database.
  It allows the agent (or workflows) to:
    - Load a design file (DEF/LEF/GDS) into the C++ SharedDatabase
    - Query net counts, cell counts, bounding boxes
    - Look up specific net IDs by name
    - Get pin locations for specific cells

WHY IS THIS A SEPARATE MODULE?
  Database queries are read-only and lightweight. Any workflow or module that
  needs design data asks Graph A "what nets does this design have?" and Graph A
  invokes m3, then passes the result into the requesting workflow's state.
  This prevents every other module from having its own direct DB access logic.

INTERNAL FLOW:
  parse_db_request → call_db_via_cli → format_db_response

COUPLING RULE:
  m3 never imports from m1, m2, or m4.
"""

import logging
from typing import TypedDict, Annotated, Sequence, Any
import operator

from langchain_core.messages import BaseMessage, HumanMessage
from langchain_google_genai import ChatGoogleGenerativeAI
from langgraph.graph import StateGraph, START, END

from ..utils.env_bootstrap import gemini_api_key
from ._cli_client import mcp_call, ping

logger = logging.getLogger(__name__)


class DbState(TypedDict):
    """State for the Database module subgraph."""
    messages:      Annotated[Sequence[BaseMessage], operator.add]
    db_operation:  str          # "load", "query_nets", "query_cells", "query_bbox"
    db_params:     dict[str, Any]  # {"filename": "my.def"} or {"net_name": "VDD"}
    db_result:     dict[str, Any]  # Returned data from C++ DB server
    error:         str


async def parse_db_request(state: DbState) -> dict:
    """
    Validate the requested DB operation.

    SUPPORTED OPERATIONS:
      "load"        — Load a design file: params need {"filename": "path/to/file.def"}
      "query_nets"  — Return all net IDs + names
      "query_cells" — Return all cell instance IDs + positions
      "query_bbox"  — Return entire design bounding box
      "status"      — Return is_loaded, design_name, net count

    If operation is unknown, return an error immediately.
    """
    op = state.get("db_operation", "")
    valid_ops = {"load", "query_nets", "query_cells", "query_bbox", "status"}
    if op not in valid_ops:
        return {"error": f"Unknown DB operation '{op}'. Valid: {valid_ops}"}

    if op == "load" and not state.get("db_params", {}).get("filename"):
        return {"error": "load operation requires 'filename' in db_params."}

    daemon_up = await ping()
    if not daemon_up:
        return {"error": "EDA daemon not running. Cannot access database."}

    return {"error": ""}


async def call_db_via_cli(state: DbState) -> dict:
    """
    Forward the DB operation as a JSON-RPC call to the eda_cli daemon.

    METHOD MAPPING:
      "load"        → "load_design" (params: {"filename": "..."})
      "query_nets"  → "db.query_nets" (no params)
      "query_cells" → "db.query_cells" (no params)
      "query_bbox"  → "db.query_bbox" (no params)
      "status"      → "db.status" (no params)

    The C++ DbMcpServer handles all of these methods.
    """
    if state.get("error"):
        return {}

    op     = state["db_operation"]
    params = state.get("db_params", {})

    method_map = {
        "load":        "load_design",
        "query_nets":  "db.query_nets",
        "query_cells": "db.query_cells",
        "query_bbox":  "db.query_bbox",
        "status":      "db.status",
    }
    method = method_map[op]

    logger.info("[DB] Calling %s with %s", method, params)
    try:
        result = await mcp_call(method, params)
        return {"db_result": result}
    except RuntimeError as e:
        return {"error": str(e), "db_result": {}}


async def format_db_response(state: DbState) -> dict:
    """Convert raw DB JSON into a human-readable explanation."""
    llm = ChatGoogleGenerativeAI(
        model="gemini-2.0-flash",
        temperature=0.2,
        api_key=gemini_api_key(),
    )

    if state.get("error"):
        prompt = f"Database operation failed: {state['error']}\nExplain and suggest next steps."
        return {"messages": [llm.invoke([HumanMessage(content=prompt)])]}

    r   = state.get("db_result", {})
    op  = state.get("db_operation", "")
    prompt = (
        f"Database operation '{op}' returned:\n{r}\n\n"
        f"Summarise the key information for a chip designer in 2-3 sentences. "
        f"Use plain language, avoid raw JSON in the output."
    )
    return {"messages": [llm.invoke([HumanMessage(content=prompt)])]}


def create_db_subgraph() -> StateGraph:
    """Builds and compiles the DB reader module subgraph (m3)."""
    wf = StateGraph(DbState)
    wf.add_node("parse",    parse_db_request)
    wf.add_node("call_cli", call_db_via_cli)
    wf.add_node("format",   format_db_response)
    wf.add_edge(START,      "parse")
    wf.add_edge("parse",    "call_cli")
    wf.add_edge("call_cli", "format")
    wf.add_edge("format",   END)
    return wf.compile()
