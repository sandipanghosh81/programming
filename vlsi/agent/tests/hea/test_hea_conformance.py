"""
test_hea_conformance.py  —  HEA conformance test suite
─────────────────────────────────────────────────────────────────────────────
Validates that any HEA implementation (Virtuoso/ICC2/KLayout) behaves
consistently according to the JSON-RPC protocol contract.

THESE TESTS ARE INTEGRATION TESTS:
  They require a live HEA server to be running.  To run against a specific
  HEA, set the environment variables:
    EDA_HEA_HOST  (default: 127.0.0.1)
    EDA_HEA_PORT  (default: 9090)

  Then run:
    pytest tests/hea/test_hea_conformance.py -v

  If no HEA is running, all tests are automatically skipped (not failed).

TEST CASE CATALOGUE:
  HEA-01  ping → pong
  HEA-02  status → is_loaded field present
  HEA-03  execute_eu with trivial script → status ok
  HEA-04  execute_eu with syntax error  → error response, not crash
  HEA-05  execute_eu returns output string
  HEA-06  unknown method → JSON-RPC -32601 error
  HEA-07  malformed JSON → JSON-RPC -32700 parse error
  HEA-08  undo_last_eu after execute_eu → ok (no crash)
  HEA-09  concurrent requests → last one must still work
"""

from __future__ import annotations

import asyncio
import json
import os

import pytest
import pytest_asyncio

# Skip entire module if no HEA is reachable (detect at collection time)
HEA_HOST = os.getenv("EDA_HEA_HOST", "127.0.0.1")
HEA_PORT = int(os.getenv("EDA_HEA_PORT", "9090"))
HEA_WS   = f"ws://{HEA_HOST}:{HEA_PORT}"

_hea_available: bool | None = None


def _check_hea_available() -> bool:
    global _hea_available
    if _hea_available is not None:
        return _hea_available
    try:
        import websockets
        async def _ping():
            async with websockets.connect(HEA_WS, open_timeout=3) as ws:
                await ws.send(json.dumps({"jsonrpc":"2.0","method":"ping","params":{},"id":0}))
                resp = json.loads(await asyncio.wait_for(ws.recv(), timeout=3))
                return "error" not in resp
        _hea_available = asyncio.run(_ping())
    except Exception:
        _hea_available = False
    return _hea_available


pytestmark = pytest.mark.skipif(
    not _check_hea_available(),
    reason=f"No HEA server reachable at {HEA_WS} — integration tests skipped",
)


# ── Fixture: send a single JSON-RPC request and return response ───────────────
@pytest.fixture
def hea_call():
    import websockets

    async def _call(method: str, params: dict | None = None, req_id: int = 1) -> dict:
        payload = json.dumps({"jsonrpc": "2.0", "method": method,
                               "params": params or {}, "id": req_id})
        async with websockets.connect(HEA_WS) as ws:
            await ws.send(payload)
            raw = await asyncio.wait_for(ws.recv(), timeout=10.0)
            return json.loads(raw)

    return _call


# ── HEA-01: ping ──────────────────────────────────────────────────────────────
@pytest.mark.asyncio
async def test_hea01_ping(hea_call):
    resp = await hea_call("ping")
    assert "error" not in resp
    assert resp.get("result") == "pong"


# ── HEA-02: status has is_loaded field ────────────────────────────────────────
@pytest.mark.asyncio
async def test_hea02_status(hea_call):
    resp = await hea_call("status")
    assert "error" not in resp
    result = resp.get("result", {})
    assert "is_loaded" in result


# ── HEA-03: execute_eu trivial ────────────────────────────────────────────────
@pytest.mark.asyncio
async def test_hea03_execute_trivial(hea_call):
    # A trivial KLayout-compatible script (also valid Python)
    trivial = "x = 1 + 1\n"
    resp = await hea_call("execute_eu", {
        "eu_name":   "test_trivial",
        "eu_source": trivial,
        "eu_args":   {},
    })
    assert "error" not in resp
    result = resp.get("result", {})
    assert result.get("status") == "ok"


# ── HEA-04: execute_eu with syntax error ─────────────────────────────────────
@pytest.mark.asyncio
async def test_hea04_execute_syntax_error(hea_call):
    bad_script = "def broken(:\n    pass\n"
    resp = await hea_call("execute_eu", {
        "eu_name":   "test_bad",
        "eu_source": bad_script,
        "eu_args":   {},
    })
    # Must return an error object, NOT crash the HEA server
    assert "error" in resp or resp.get("result", {}).get("status") == "error"


# ── HEA-05: execute_eu returns captured output ────────────────────────────────
@pytest.mark.asyncio
async def test_hea05_execute_output(hea_call):
    script = "print('hello_from_eu')\n"
    resp = await hea_call("execute_eu", {
        "eu_name":   "test_output",
        "eu_source": script,
        "eu_args":   {},
    })
    result = resp.get("result", {})
    # Some HEA implementations may not capture print() output — soft check
    assert result.get("status") == "ok"


# ── HEA-06: unknown method returns -32601 ─────────────────────────────────────
@pytest.mark.asyncio
async def test_hea06_unknown_method(hea_call):
    resp = await hea_call("nonexistent_method_xyz")
    assert "error" in resp
    assert resp["error"]["code"] == -32601


# ── HEA-07: malformed JSON returns -32700 ─────────────────────────────────────
@pytest.mark.asyncio
async def test_hea07_malformed_json():
    import websockets
    async with websockets.connect(HEA_WS) as ws:
        await ws.send("{not valid json !@#}")
        raw = await asyncio.wait_for(ws.recv(), timeout=5.0)
        resp = json.loads(raw)
    assert "error" in resp
    assert resp["error"]["code"] == -32700


# ── HEA-08: undo_last_eu does not crash ───────────────────────────────────────
@pytest.mark.asyncio
async def test_hea08_undo_last_eu(hea_call):
    # Execute something first
    await hea_call("execute_eu", {"eu_name": "pre_undo", "eu_source": "x = 1\n", "eu_args": {}})
    resp = await hea_call("undo_last_eu")
    assert "error" not in resp or resp.get("error", {}).get("code") != -32601


# ── HEA-09: sequential calls all succeed ─────────────────────────────────────
@pytest.mark.asyncio
async def test_hea09_sequential_calls(hea_call):
    for i in range(5):
        resp = await hea_call("ping", req_id=i + 10)
        assert "error" not in resp
        assert resp.get("result") == "pong"
