"""
server.py  —  FastAPI Entry Point: The Bridge Between KLayout and Graph A
═══════════════════════════════════════════════════════════════════════════════

WHAT THIS FILE IS:
  The HTTP server that KLayout's chatbot macro talks to.
  It:
    1. Receives POST /chat from the KLayout macro
    2. Passes the message to Graph A (orchestrator)
    3. Returns a ChatResponse containing the AI reply AND viewer_commands
  
  KLayout then executes viewer_commands locally on its canvas.

STARTUP SEQUENCE:
  1. python server.py               (or: uvicorn server:app)
  2. FastAPI starts on port 8000
  3. LangGraph Graph A is compiled (once, at import time)
  4. Server is ready to handle requests

WHY FASTAPI?
  FastAPI gives us:
    - Async support (critical: Graph A uses async/await for websocket calls)
    - Automatic request/response schema validation (Pydantic)
    - Automatic interactive API docs at http://127.0.0.1:8000/docs
  The interactive docs page lets you test the API without KLayout.

CONCURRENCY NOTE:
  FastAPI + uvicorn serve requests concurrently using asyncio.
  Graph A's nodes use `async def` so multiple KLayout users could connect
  simultaneously (each gets their own graph invocation instance).
  However, all share one eda_cli daemon — the daemon handles concurrency
  internally (see eda_cli/src/eda_daemon.cpp for per-request threading).
"""

import os
import logging
import sys
from pathlib import Path
from typing import List, Dict, Any

import uvicorn
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
from langchain_core.messages import HumanMessage

# Ensure src/ is importable (src-layout project).
_HERE = Path(__file__).resolve().parent
_SRC = _HERE / "src"
if str(_SRC) not in sys.path:
    sys.path.insert(0, str(_SRC))

# Load vlsi/agent/.env (override=True) before LangChain / Gemini imports.
from orchestrator.utils.env_bootstrap import load_agent_env

load_agent_env()

from orchestrator.orchestrator_graph import create_orchestrator_graph

# ─── Logging setup ────────────────────────────────────────────────────────────
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  %(name)-30s  %(levelname)-8s  %(message)s",
)
logger = logging.getLogger(__name__)

# ─── FastAPI app ──────────────────────────────────────────────────────────────
app = FastAPI(
    title="VLSI LangGraph Agent Server",
    description=(
        "Receives natural-language EDA commands from the KLayout chatbot macro "
        "and routes them through the LangGraph orchestrator to the C++ EDA engine."
    ),
    version="1.0.0",
)

# ── Compile Graph A ONCE at startup.  Subsequent requests reuse this instance.
# WHY: Compilation involves building the Python graph topology and compiling
# each subgraph.  This takes ~0.5–2s.  Doing it per-request would add unacceptable
# latency.  Instead we compile once and each ainvoke() creates a new run instance.
logger.info("Compiling LangGraph orchestrator (Graph A)...")
master_graph = create_orchestrator_graph()
logger.info("Graph A ready.")


# ─── Request / Response schemas ───────────────────────────────────────────────
class ChatRequest(BaseModel):
    """
    WHAT THE KLAYOUT MACRO SENDS:
      {"message": "route the power rail VDD", "layer_pref": "M2"}

    message:     The raw text the user typed in the KLayout chatbot dock widget.
    layer_pref:  Optional preferred routing layer hint (e.g. "M1", "M2").
                 Passed into intent_params so the router subgraph can use it.
    session_id:  Optional session identifier for multi-turn trace correlation.
    """
    message:    str
    layer_pref: str | None = None
    session_id: str | None = None


class ChatResponse(BaseModel):
    """
    WHAT THE KLAYOUT MACRO RECEIVES:
      {"reply": "Routing completed...", "viewer_commands": [{"action": "refresh_view"}]}

    reply:          The AI's natural-language response text.  Displayed in the chat history.
    viewer_commands: List of action dicts the KLayout macro executes on its canvas.
                    See klayout_macro/chatbot_dock.py::process_commands() for the
                    list of supported actions.
    """
    reply:           str
    viewer_commands: List[Dict[str, Any]]


# ─── Health check endpoint ────────────────────────────────────────────────────
@app.get("/health")
async def health_check():
    """
    WHAT IT DOES: Detailed health check — probes all downstream services.
    USAGE: curl http://127.0.0.1:8000/health
    RETURNS:
      {
        "status": "ok" | "degraded",
        "agent": "ready",
        "services": {
          "eda_daemon":  "ok" | "unreachable",
          "hea":         "ok" | "unreachable",
          "chromadb":    "ok" | "unreachable",
          "klayout_drc": "ok" | "unreachable"
        }
      }
    status="degraded" means the agent works but some services are unavailable.
    The agent uses graceful degradation for all non-critical services.
    """
    import asyncio as _asyncio

    async def _probe_daemon():
        try:
            from orchestrator.modules._cli_client import ping
            return "ok" if await ping() else "unreachable"
        except Exception:
            return "unreachable"

    async def _probe_hea():
        try:
            from orchestrator.modules._hea_client import hea_ping
            return "ok" if await hea_ping() else "unreachable"
        except Exception:
            return "unreachable"

    async def _probe_chromadb():
        try:
            from orchestrator.rag.rag_service import get_rag_client
            get_rag_client()
            return "ok"
        except Exception:
            return "unreachable"

    async def _probe_klayout():
        try:
            from orchestrator.drc.klayout_client import klayout_health_check
            return "ok" if await klayout_health_check() else "unreachable"
        except Exception:
            return "unreachable"

    daemon_s, hea_s, chroma_s, klayout_s = await _asyncio.gather(
        _probe_daemon(), _probe_hea(), _probe_chromadb(), _probe_klayout(),
        return_exceptions=True,
    )

    services = {
        "eda_daemon":  str(daemon_s)  if not isinstance(daemon_s,  Exception) else "unreachable",
        "hea":         str(hea_s)     if not isinstance(hea_s,     Exception) else "unreachable",
        "chromadb":    str(chroma_s)  if not isinstance(chroma_s,  Exception) else "unreachable",
        "klayout_drc": str(klayout_s) if not isinstance(klayout_s, Exception) else "unreachable",
    }

    all_ok = all(v == "ok" for v in services.values())
    return {
        "status":   "ok" if all_ok else "degraded",
        "agent":    "ready",
        "services": services,
    }


# ─── Main chat endpoint ───────────────────────────────────────────────────────
@app.post("/chat", response_model=ChatResponse)
async def chat_endpoint(request: ChatRequest):
    """
    WHAT IT DOES (step by step):
      1. Receives {"message": "..."} from KLayout macro (POST body)
      2. Wraps message in HumanMessage and builds the initial OrchestratorState
      3. Calls master_graph.ainvoke(initial_state) — async, may take 1-30s
         depending on whether routing/placement is involved
      4. Extracts the last AI message from the final state's messages list
      5. Collects viewer_commands from the final state
      6. Returns {"reply": "...", "viewer_commands": [...]}

    ERROR HANDLING:
      Any exception from the graph is caught and returned as an HTTP 500 error
      with a user-friendly message.  The KLayout macro displays this in the chat.

    INTERACTIVE TESTING:
      Visit http://127.0.0.1:8000/docs to test this endpoint in your browser
      without needing KLayout running.
    """
    logger.info("[Server] Received: '%s'", request.message[:80])

    from orchestrator.tracing import new_job_id
    job_id = request.session_id or new_job_id()

    initial_state = {
        "messages":        [HumanMessage(content=request.message)],
        "active_intent":   "none",
        "intent_params":   {
            **({"layer_pref": request.layer_pref} if request.layer_pref else {}),
        },
        "viewer_commands": [],
        "layout_locked":   True,
        "job_id":          job_id,
    }

    try:
        final_state = await master_graph.ainvoke(initial_state)
    except Exception as e:
        logger.exception("[Server] Graph invocation failed")
        raise HTTPException(
            status_code=500,
            detail=f"Agent error: {e}"
        ) from e

    # Extract the last AI message from the message list
    reply = "(No response generated)"
    for msg in reversed(final_state.get("messages", [])):
        if hasattr(msg, "type") and msg.type == "ai":
            reply = msg.content
            break

    cmds = final_state.get("viewer_commands", [])
    logger.info("[Server] Returning reply (%d chars), %d viewer commands", len(reply), len(cmds))

    return ChatResponse(reply=reply, viewer_commands=cmds)


# ─── Entry point ─────────────────────────────────────────────────────────────
if __name__ == "__main__":
    logger.info("Starting VLSI LangGraph Agent Server on http://127.0.0.1:8000")
    logger.info("API docs: http://127.0.0.1:8000/docs")
    logger.info("Ensure the C++ eda_cli daemon is running on ws://127.0.0.1:8080")
    uvicorn.run(
        "server:app",
        host="127.0.0.1",
        port=8000,
        log_level="info",
        reload=False,  # Set True during development to auto-reload on file changes
    )
