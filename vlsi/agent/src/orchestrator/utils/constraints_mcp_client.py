from __future__ import annotations

import asyncio
import json
import os
from typing import Any


CONSTRAINTS_HOST = os.getenv("CONSTRAINTS_MCP_HOST", "127.0.0.1")
CONSTRAINTS_PORT = int(os.getenv("CONSTRAINTS_MCP_PORT", "18081"))
CONSTRAINTS_TIMEOUT = float(os.getenv("CONSTRAINTS_MCP_TIMEOUT", "60.0"))
CONSTRAINTS_WS_URL = f"ws://{CONSTRAINTS_HOST}:{CONSTRAINTS_PORT}"

_req_id = 0


def _next_id() -> int:
    global _req_id
    _req_id += 1
    return _req_id


async def mcp_call_constraints(method: str, params: dict[str, Any] | None = None) -> Any:
    """
    Call the constraints MCP server (JSON-RPC 2.0 over WebSocket).
    """
    try:
        import websockets
    except ImportError as e:
        raise RuntimeError("The 'websockets' package is not installed. Run: pip install websockets") from e

    payload = json.dumps(
        {"jsonrpc": "2.0", "method": method, "params": params or {}, "id": _next_id()}
    )

    async with websockets.connect(CONSTRAINTS_WS_URL) as ws:
        await ws.send(payload)
        raw = await asyncio.wait_for(ws.recv(), timeout=CONSTRAINTS_TIMEOUT)

    resp = json.loads(raw)
    if "error" in resp:
        raise RuntimeError(f"constraints MCP error for '{method}': {resp['error']}")
    return resp.get("result")

