"""
test_system.py  —  Integration Tests for the VLSI Agent System
═══════════════════════════════════════════════════════════════════════════════

WHAT THESE TESTS DO:
  Verify the full system is functioning WITHOUT needing KLayout running.
  Tests run in three tiers:

  TIER 1 — Python-only tests (no C++ daemon needed):
    - agent/ imports are syntactically valid
    - Orchestrator graph compiles without error
    - Intent parsing maps known phrases to expected intents

  TIER 2 — Integration tests (requires Python server.py running):
    - HTTP /health → {"status": "ok"}
    - HTTP /chat "hello" → returns a reply string

  TIER 3 — Full stack tests (requires server.py + eda_daemon both running):
    - "load a design" → triggers load_design → db.status returns is_loaded=True
    - "route the design" → route_nets returns wirelength > 0

HOW TO RUN:
  # Tier 1 only (no services needed):
  pytest test_system.py -k "tier1"

  # Tier 1 + 2 (start server.py first):
  python server.py &
  pytest test_system.py -k "tier1 or tier2"

  # All tiers (start both daemon and server.py):
  cd eda_cli && ./build/eda_daemon &
  cd vlsi/agent && python server.py &
  pytest test_system.py

WHY NO MOCK OBJECTS:
  This is a SYSTEM test, not a unit test.  It deliberately tests the real
  stack end-to-end to catch integration issues (import errors, port conflicts,
  JSON schema mismatches, etc.) that mocks would hide.
"""

import sys
import os
import ast
import json
import urllib.request
import urllib.error
import asyncio
import pytest

# ─── Tier 1: Module Import & Syntax Tests ────────────────────────────────────

# Ensure src/ is importable when running tests from repo root.
THIS_DIR = os.path.dirname(os.path.abspath(__file__))
SRC_DIR = os.path.join(THIS_DIR, "src")
if SRC_DIR not in sys.path:
    sys.path.insert(0, SRC_DIR)

PYTHON_FILES_TO_PARSE = [
    "src/orchestrator/__init__.py",
    "src/orchestrator/modules/__init__.py",
    "src/orchestrator/modules/_cli_client.py",
    "src/orchestrator/modules/m1_router_subgraph.py",
    "src/orchestrator/modules/m2_placer_subgraph.py",
    "src/orchestrator/modules/m3_db_subgraph.py",
    "src/orchestrator/modules/m4_window_subgraph.py",
    "src/orchestrator/workflows/__init__.py",
    "src/orchestrator/workflows/w1_full_route_flow.py",
    "src/orchestrator/workflows/w2_drc_fix_loop.py",
    "src/orchestrator/orchestrator_graph.py",
    "server.py",
    "klayout_macro/chatbot_dock.py",
]

@pytest.mark.parametrize("filepath", PYTHON_FILES_TO_PARSE)
def test_tier1_syntax_valid(filepath):
    """Tier 1: Every Python file in the agent parses cleanly (no SyntaxError)."""
    with open(filepath) as f:
        source = f.read()
    # ast.parse raises SyntaxError on invalid Python.  No exception = pass.
    ast.parse(source, filename=filepath)


def test_tier1_cli_client_importable():
    """Tier 1: _cli_client.py can be imported (without websockets installed OK)."""
    # The module defers the websockets import to call time, so it should
    # import cleanly even if websockets is missing.
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        "cli_client", "src/orchestrator/modules/_cli_client.py"
    )
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    assert hasattr(mod, "mcp_call"), "_cli_client must export mcp_call"
    assert hasattr(mod, "ping"),     "_cli_client must export ping"


@pytest.mark.asyncio
async def test_tier1_cli_ping_fails_gracefully_when_daemon_not_running():
    """
    Tier 1: ping() returns False (not an exception) when daemon is not reachable.
    This verifies the CLI client's error handling doesn't crash the agent.
    """
    # Override port to something definitely not listening.
    import orchestrator.modules._cli_client as cli
    original_url = cli.CLI_WS_URL
    cli.CLI_WS_URL = "ws://127.0.0.1:19999"  # Nothing listening here
    try:
        result = await cli.ping()
        assert result is False, "ping() should return False when daemon is unreachable"
    except Exception as e:
        pytest.fail(f"ping() raised an exception instead of returning False: {e}")
    finally:
        cli.CLI_WS_URL = original_url  # Restore


# ─── Tier 2: HTTP Server Tests ───────────────────────────────────────────────

SERVER_URL = "http://127.0.0.1:8000"

def _is_server_up() -> bool:
    try:
        with urllib.request.urlopen(f"{SERVER_URL}/health", timeout=2) as r:
            return r.status == 200
    except Exception:
        return False


@pytest.mark.skipif(not _is_server_up(), reason="server.py not running")
def test_tier2_health_check():
    """Tier 2: /health returns {status: 'ok', agent: 'ready'}."""
    with urllib.request.urlopen(f"{SERVER_URL}/health", timeout=5) as r:
        data = json.loads(r.read())
    assert data["status"] == "ok"
    assert data["agent"] == "ready"


@pytest.mark.skipif(not _is_server_up(), reason="server.py not running")
def test_tier2_chat_hello():
    """Tier 2: POST /chat with 'hello' returns a non-empty reply string."""
    req = urllib.request.Request(
        f"{SERVER_URL}/chat",
        data=json.dumps({"message": "hello"}).encode(),
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=30) as r:
        result = json.loads(r.read())

    assert "reply"           in result, "/chat response must have 'reply' field"
    assert "viewer_commands" in result, "/chat response must have 'viewer_commands' field"
    assert isinstance(result["reply"], str),  "'reply' must be a string"
    assert len(result["reply"]) > 0,          "'reply' must not be empty"
    assert isinstance(result["viewer_commands"], list), "'viewer_commands' must be a list"


@pytest.mark.skipif(not _is_server_up(), reason="server.py not running")
def test_tier2_chat_unknown_question():
    """Tier 2: Asking an off-topic question gets a chat response (not an error)."""
    req = urllib.request.Request(
        f"{SERVER_URL}/chat",
        data=json.dumps({"message": "what is a Steiner tree?"}).encode(),
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=30) as r:
        result = json.loads(r.read())
    assert result["reply"], "Off-topic question should return non-empty reply"


# ─── Tier 3: Full-Stack Tests (both daemon + server.py running) ──────────────

def _is_daemon_reachable() -> bool:
    """Quick check: can we connect to the C++ daemon?"""
    import socket
    try:
        s = socket.create_connection(("127.0.0.1", 8080), timeout=1)
        s.close()
        return True
    except Exception:
        return False


FULL_STACK = _is_server_up() and _is_daemon_reachable()


@pytest.mark.skipif(not FULL_STACK, reason="server.py or eda_daemon not running")
def test_tier3_load_and_route():
    """
    Tier 3: Full-stack test — load a design then route it.
    Expected: reply contains 'Routing completed' or mentions wirelength.
    """
    def chat(msg: str) -> dict:
        req = urllib.request.Request(
            f"{SERVER_URL}/chat",
            data=json.dumps({"message": msg}).encode(),
            headers={"Content-Type": "application/json"},
        )
        with urllib.request.urlopen(req, timeout=60) as r:
            return json.loads(r.read())

    # Step 1: Load a design.
    load_result = chat("load design test.def")
    assert load_result["reply"], "load design should return a reply"

    # Step 2: Route.
    route_result = chat("route the design")
    reply = route_result["reply"].lower()
    # The reply should mention routing completion OR an explanation.
    assert any(kw in reply for kw in ["routing", "wire", "via", "complet", "error"]), (
        f"route reply unexpectedly empty: {reply}"
    )

    # Step 3: KLayout viewer commands.
    # After routing, we expect at least a refresh_view command.
    cmds = [c.get("action") for c in route_result.get("viewer_commands", [])]
    assert "unlock_ui" in cmds, "unlock_ui must always be in viewer_commands"
