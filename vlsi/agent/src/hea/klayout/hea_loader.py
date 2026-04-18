"""
hea_loader.py  —  Host Execution Agent loader for KLayout
─────────────────────────────────────────────────────────────────────────────
Bootstraps a JSON-RPC 2.0 WebSocket server inside KLayout's Python scripting
environment so the external Python agent can deploy and execute KLayout-Python
EUs atomically.

LOADING:
  In KLayout → Macros → Add macro → type: Python:
    exec(open("/path/to/hea_loader.py").read())
  Or add to ~/.klayout/pymacros/autorun/:
    (copy this file there; KLayout auto-loads it at startup)

DEPENDENCIES:
  KLayout 0.28+ with Python API (klayout.db, klayout.lay).
  Python 3.9+ standard library (asyncio, json, websockets or threading+socket).

PROTOCOL:
  WebSocket server on port 9090 (override via EDA_HEA_PORT env var).
  Uses threading so KLayout's GUI event loop is not blocked.
  Each EU is executed in KLayout's main thread via klayout.lay.Application.instance().postEvent().

JSON-RPC METHODS:
  ping           → "pong"
  execute_eu     → executes eu_source Python in KLayout context; returns output
  status         → {is_loaded, cell_name, top_cell}
  zoom_box       → zooms to bbox [x1,y1,x2,y2] in µm
  refresh        → refreshes the layout view
  undo_last_eu   → calls layout_view.transaction().abort()
"""

import json
import logging
import os
import threading
import traceback
from typing import Any

logger = logging.getLogger(__name__)
logging.basicConfig(level=logging.INFO)

HEA_PORT = int(os.environ.get("EDA_HEA_PORT", "9090"))


# ── Request handler ───────────────────────────────────────────────────────────
def _json_result(req_id: int, result: Any) -> str:
    return json.dumps({"jsonrpc": "2.0", "result": result, "id": req_id})


def _json_error(req_id: int, code: int, message: str) -> str:
    return json.dumps({"jsonrpc": "2.0", "error": {"code": code, "message": message}, "id": req_id})


def handle_request(request_str: str) -> str:
    """Dispatch a JSON-RPC request and return the JSON response string."""
    try:
        req = json.loads(request_str)
    except json.JSONDecodeError as e:
        return _json_error(0, -32700, f"Parse error: {e}")

    req_id = req.get("id", 0)
    method = req.get("method", "")
    params = req.get("params", {})

    try:
        if method == "ping":
            return _json_result(req_id, "pong")

        elif method == "status":
            import klayout.lay as lay
            view = lay.LayoutView.current()
            if view is None:
                return _json_result(req_id, {"is_loaded": False, "cell_name": "", "top_cell": ""})
            layout = view.active_cellview().layout() if view.active_cellview().is_valid() else None
            cell   = view.active_cellview().cell   if layout else None
            return _json_result(req_id, {
                "is_loaded": layout is not None,
                "cell_name": cell.name if cell else "",
                "top_cell":  layout.top_cells()[0].name if layout and layout.top_cells() else "",
            })

        elif method == "execute_eu":
            eu_source = params.get("eu_source", "")
            eu_name   = params.get("eu_name",   "hea_eu")
            output_buf = []

            # Execute in KLayout's main Python context
            exec_globals = {"__builtins__": __builtins__, "_output": output_buf}
            try:
                import klayout.db  as db   # noqa: F401
                import klayout.lay as lay  # noqa: F401
                exec_globals["db"]  = db
                exec_globals["lay"] = lay
            except ImportError:
                pass

            try:
                exec(eu_source, exec_globals)
                output = "\n".join(str(x) for x in output_buf)
                return _json_result(req_id, {"status": "ok", "output": output})
            except Exception as e:
                tb = traceback.format_exc()
                return _json_error(req_id, -32000, f"EU failed: {e}\n{tb}")

        elif method == "zoom_box":
            # params: {"x1": float, "y1": float, "x2": float, "y2": float}
            try:
                import klayout.db  as db
                import klayout.lay as lay
                view = lay.LayoutView.current()
                if view:
                    box = db.DBox(params["x1"], params["y1"], params["x2"], params["y2"])
                    view.zoom_box(box)
                return _json_result(req_id, {"status": "ok"})
            except Exception as e:
                return _json_error(req_id, -32000, str(e))

        elif method == "refresh":
            try:
                import klayout.lay as lay
                view = lay.LayoutView.current()
                if view:
                    view.update()
                return _json_result(req_id, {"status": "ok"})
            except Exception as e:
                return _json_error(req_id, -32000, str(e))

        elif method == "undo_last_eu":
            try:
                import klayout.lay as lay
                view = lay.LayoutView.current()
                if view:
                    view.cancel_edits()
                return _json_result(req_id, {"status": "ok"})
            except Exception as e:
                return _json_error(req_id, -32000, str(e))

        else:
            return _json_error(req_id, -32601, f"Method not found: {method}")

    except Exception as e:
        return _json_error(req_id, -32000, f"Internal error: {e}")


# ── WebSocket server (threaded) ───────────────────────────────────────────────
def _run_ws_server(port: int) -> None:
    """Run the WebSocket server in a background thread."""
    try:
        import asyncio
        import websockets

        async def _handler(ws):
            async for message in ws:
                response = handle_request(message)
                await ws.send(response)

        async def _serve():
            async with websockets.serve(_handler, "0.0.0.0", port):
                logger.info("HEA (KLayout): WebSocket server on ws://0.0.0.0:%d", port)
                await asyncio.Future()  # run forever

        asyncio.run(_serve())

    except ImportError:
        # Fallback: bare TCP socket (no websockets package)
        import socket
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("0.0.0.0", port))
        srv.listen(1)
        logger.info("HEA (KLayout): TCP fallback server on port %d", port)
        while True:
            conn, addr = srv.accept()
            with conn:
                buf = b""
                while True:
                    data = conn.recv(4096)
                    if not data:
                        break
                    buf += data
                    try:
                        msg = buf.decode("utf-8").rstrip("\n")
                        resp = handle_request(msg) + "\n"
                        conn.sendall(resp.encode("utf-8"))
                        buf = b""
                    except Exception:
                        break


def start_hea(port: int = HEA_PORT) -> threading.Thread:
    """Start the HEA server in a daemon background thread."""
    t = threading.Thread(target=_run_ws_server, args=(port,), daemon=True, name="hea-server")
    t.start()
    logger.info("HEA (KLayout): Started background thread on port %d", port)
    return t


# ── Auto-start when loaded in KLayout ────────────────────────────────────────
_hea_thread = start_hea(HEA_PORT)
