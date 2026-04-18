"""
_hea_client.py  —  Host Execution Agent (HEA) WebSocket client
─────────────────────────────────────────────────────────────────────────────
Mirrors _cli_client.py in purpose but talks to the HEA (the tiny MCP server
embedded inside the EDA host) instead of the C++ daemon.

PROTOCOL:
  Same JSON-RPC 2.0 over WebSocket as the C++ daemon, but with an extra
  "eu_source" field that carries the compiled EU script body.

  Request:
    {
      "jsonrpc": "2.0",
      "method":  "execute_eu",
      "params":  {
        "eu_name":    "route_nets_virtuoso",
        "eu_source":  "<compiled SKILL/Tcl/Python script as string>",
        "eu_args":    { ... },   // template-substituted values
        "timeout_s":  120
      },
      "id": 5
    }

  Response:
    { "jsonrpc": "2.0", "result": { "status": "ok", "output": "..." }, "id": 5 }
    { "jsonrpc": "2.0", "error":  { "code": -32000, "message": "..." }, "id": 5 }

HEA ADDRESS:
  Default: ws://host.docker.internal:9090  (HEA runs in the EDA host on the
  Docker host machine).  Override with EDA_HEA_HOST / EDA_HEA_PORT env vars.
"""

import json
import asyncio
import logging
import os
from typing import Any

logger = logging.getLogger(__name__)

HEA_HOST    = os.getenv("EDA_HEA_HOST", "host.docker.internal")
HEA_PORT    = int(os.getenv("EDA_HEA_PORT", "9090"))
HEA_TIMEOUT = float(os.getenv("EDA_HEA_TIMEOUT", "120.0"))

HEA_WS_URL = f"ws://{HEA_HOST}:{HEA_PORT}"

_request_id = 0


def _next_id() -> int:
    global _request_id
    _request_id += 1
    return _request_id


async def execute_eu(
    eu_name: str,
    eu_source: str,
    eu_args: dict[str, Any] | None = None,
    timeout_s: float | None = None,
) -> dict[str, Any]:
    """
    Deploy and execute a compiled EU on the HEA.

    Args:
        eu_name:   Human-readable name for logging and tracing.
        eu_source: The rendered (Jinja2-expanded) script body as a string.
        eu_args:   Extra key-value arguments passed alongside the source.
        timeout_s: Override per-call timeout (default: HEA_TIMEOUT).

    Returns:
        The "result" dict from the HEA JSON-RPC response.

    Raises:
        RuntimeError if HEA is unreachable or returns an error.
        asyncio.TimeoutError if the EDA host does not respond in time.
    """
    try:
        import websockets
    except ImportError as e:
        raise RuntimeError("websockets package not installed: pip install websockets") from e

    timeout = timeout_s if timeout_s is not None else HEA_TIMEOUT
    req_id  = _next_id()
    payload = json.dumps({
        "jsonrpc":  "2.0",
        "method":   "execute_eu",
        "params":   {
            "eu_name":   eu_name,
            "eu_source": eu_source,
            "eu_args":   eu_args or {},
            "timeout_s": timeout,
        },
        "id": req_id,
    })

    logger.info("[HEA Client] → execute_eu '%s' (timeout=%.0fs)", eu_name, timeout)

    try:
        async with websockets.connect(HEA_WS_URL) as ws:
            await ws.send(payload)
            raw = await asyncio.wait_for(ws.recv(), timeout=timeout + 5.0)
    except ConnectionRefusedError as e:
        raise RuntimeError(
            f"Cannot connect to HEA at {HEA_WS_URL}. "
            f"Is the EDA host running the HEA loader?"
        ) from e

    response = json.loads(raw)
    logger.debug("[HEA Client] ← %s", response)

    if "error" in response:
        raise RuntimeError(
            f"HEA returned error for EU '{eu_name}': {response['error']}"
        )

    return response.get("result", {})


async def hea_ping() -> bool:
    """Quick health check — returns True if the HEA is reachable."""
    try:
        import websockets
        req_id  = _next_id()
        payload = json.dumps({"jsonrpc": "2.0", "method": "ping", "params": {}, "id": req_id})
        async with websockets.connect(HEA_WS_URL) as ws:
            await ws.send(payload)
            raw = await asyncio.wait_for(ws.recv(), timeout=5.0)
        response = json.loads(raw)
        return "error" not in response
    except Exception:
        return False
