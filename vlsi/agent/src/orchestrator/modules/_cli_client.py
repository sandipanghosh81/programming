"""
agent/modules/_cli_client.py  —  Shared WebSocket MCP Client
═══════════════════════════════════════════════════════════════════════════════

WHAT THIS MODULE IS:
  The single, shared gateway that ALL module subgraphs (m1, m2, m3, m4) use to
  talk to the C++ eda_cli daemon over WebSocket (JSON-RPC 2.0).

  NO module imports websockets directly.  They all call mcp_call() here.

WHY ONE CLIENT FOR ALL MODULES?
  - Single point for connection config (host, port, timeout)
  - Single point for retry logic and error formatting
  - Prevents each module from having its own half-baked WebSocket handling
  - ANALOGY: All departments in an office use one mail room, not one per floor.

WHAT IS JSON-RPC 2.0?
  A lightweight protocol where every request is:
    {"jsonrpc": "2.0", "method": "some_method", "params": {...}, "id": 1}
  And every response is EITHER:
    {"jsonrpc": "2.0", "result": {...}, "id": 1}        ← success
    {"jsonrpc": "2.0", "error": {"message":"..."}, "id": 1}  ← failure
  The "id" field lets us match responses to requests (important for async).

HOW MODULES USE THIS:
  # In m1_router_subgraph.py:
  result = await mcp_call("route_nets", {"net_ids": [0, 1, 2]})
  # Returns: {"status": "completed", "wirelength": 82540, ...}
"""

import json
import asyncio
import logging
from typing import Any

logger = logging.getLogger(__name__)

# ─── Configuration ────────────────────────────────────────────────────────────
# These match the eda_cli daemon defaults.  Override via environment variables.
import os

CLI_HOST    = os.getenv("EDA_CLI_HOST", "127.0.0.1")
CLI_PORT    = int(os.getenv("EDA_CLI_PORT", "8080"))
CLI_TIMEOUT = float(os.getenv("EDA_CLI_TIMEOUT", "30.0"))

CLI_WS_URL  = f"ws://{CLI_HOST}:{CLI_PORT}"

# Request ID counter — each call gets a unique ID so responses can be matched.
# Using a simple integer because we currently make sequential (not parallel) calls.
_request_id = 0


def _next_id() -> int:
    """Increment and return the next unique JSON-RPC request ID."""
    global _request_id
    _request_id += 1
    return _request_id


async def mcp_call(method: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
    """
    Send ONE JSON-RPC request to the C++ eda_cli daemon and return the result.

    PARAMETERS:
        method  — JSON-RPC method name, e.g. "route_nets", "load_design", "ping"
        params  — Dict of parameters for the method (can be empty {})

    RETURNS:
        The "result" dict from the JSON-RPC response.

    RAISES:
        RuntimeError  — if the daemon returns a JSON-RPC error object
        RuntimeError  — if the WebSocket connection fails (daemon not running)
        asyncio.TimeoutError — if the daemon does not respond within CLI_TIMEOUT

    ANALOGY:
        Like calling the front desk of a hotel.  You say "I need room_service"
        (method), give your room number and order (params), and wait for "your
        order is ready" (result).  If the kitchen is closed, you get an error.

    STEP BY STEP:
        1. Build the JSON-RPC envelope: {jsonrpc, method, params, id}
        2. Open a WebSocket connection to ws://127.0.0.1:8080
        3. Send the JSON string
        4. Wait for a response string (up to CLI_TIMEOUT seconds)
        5. Parse the response JSON
        6. Return result dict, or raise RuntimeError with the error message
    """
    # websockets is imported here (not at module top) so that importing this
    # module does NOT fail if websockets is not installed — it fails only when
    # actually called. This allows unit tests to mock mcp_call without
    # installing websockets.
    try:
        import websockets
    except ImportError as e:
        raise RuntimeError(
            "The 'websockets' package is not installed.  "
            "Run: pip install websockets"
        ) from e

    req_id  = _next_id()
    payload = json.dumps({
        "jsonrpc": "2.0",
        "method":  method,
        "params":  params or {},
        "id":      req_id,
    })

    logger.debug("[CLI Client] → %s  %s", method, params)

    try:
        async with websockets.connect(CLI_WS_URL) as ws:
            await ws.send(payload)
            raw = await asyncio.wait_for(ws.recv(), timeout=CLI_TIMEOUT)

    except ConnectionRefusedError as e:
        raise RuntimeError(
            f"Cannot connect to eda_cli daemon at {CLI_WS_URL}. "
            f"Is the daemon running?  Start it with: ./eda_cli/build/eda_daemon"
        ) from e

    response = json.loads(raw)
    logger.debug("[CLI Client] ← %s  %s", method, response)

    if "error" in response:
        raise RuntimeError(
            f"eda_cli returned error for '{method}': {response['error']}"
        )

    return response.get("result", {})


async def ping() -> bool:
    """
    Quick health-check: returns True if the eda_cli daemon is reachable.

    USAGE:
        if not await ping():
            print("Start the daemon first!")
    """
    try:
        result = await mcp_call("ping")
        return result == "pong" or True  # daemon returns "pong" as the result string
    except RuntimeError:
        return False
