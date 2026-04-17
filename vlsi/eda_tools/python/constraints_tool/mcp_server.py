from __future__ import annotations

import asyncio
import json
import os
from pathlib import Path
from typing import Any


def _load_constraints_module():
    """
    Load the colocated constraints.py as a module.
    """
    import importlib.util

    here = Path(__file__).resolve().parent
    p = here / "constraints.py"
    spec = importlib.util.spec_from_file_location("constraints_tool_constraints", p)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load constraints module from {p}")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _result(id_: Any, result: Any) -> dict[str, Any]:
    return {"jsonrpc": "2.0", "id": id_, "result": result}


def _error(id_: Any, message: str, code: int = -32603) -> dict[str, Any]:
    return {"jsonrpc": "2.0", "id": id_, "error": {"code": code, "message": message}}


async def _handle_request(mod, req: dict[str, Any]) -> dict[str, Any]:
    id_ = req.get("id", None)
    method = req.get("method", "")
    params = req.get("params", {}) or {}

    if method == "ping":
        return _result(id_, "pong")

    if method == "constraints.extract":
        spice_path = params.get("spice_path")
        if not spice_path:
            return _error(id_, "Missing params.spice_path", code=-32602)
        p = Path(spice_path)
        if not p.exists():
            return _error(id_, f"SPICE file not found: {p}", code=-32602)

        # Parse and distill. This uses the existing logic in constraints.py.
        netlist_text = p.read_text(encoding="utf-8")
        ckt = mod.parse_spice(netlist_text)
        design = mod.Design(ckt)
        design.distill_schematic_data()
        design.distill_layout_data()
        data = design.export_data()
        return _result(id_, data)

    return _error(id_, f"Method not found: {method}", code=-32601)


async def serve(host: str = "127.0.0.1", port: int = 18081) -> None:
    """
    Minimal JSON-RPC 2.0 server over WebSocket for constraint extraction.
    """
    try:
        import websockets
    except ImportError as e:
        raise RuntimeError("Missing dependency: websockets. Install with: pip install websockets") from e

    mod = _load_constraints_module()

    async def _ws_handler(ws):
        async for raw in ws:
            try:
                req = json.loads(raw)
            except Exception as e:
                await ws.send(json.dumps(_error(None, f"Parse error: {e}", code=-32700)))
                continue
            try:
                resp = await _handle_request(mod, req)
            except Exception as e:
                resp = _error(req.get("id"), f"Internal error: {e}")
            await ws.send(json.dumps(resp))

    print(f"[constraints_tool] listening on ws://{host}:{port}")
    async with websockets.serve(_ws_handler, host, port):
        await asyncio.Future()  # run forever


if __name__ == "__main__":
    host = os.getenv("CONSTRAINTS_MCP_HOST", "127.0.0.1")
    port = int(os.getenv("CONSTRAINTS_MCP_PORT", "18081"))
    asyncio.run(serve(host, port))

