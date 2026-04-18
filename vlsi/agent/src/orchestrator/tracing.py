"""
tracing.py  —  Structured job tracing for the VLSI agent
─────────────────────────────────────────────────────────────────────────────
Emits JSON-Lines trace events to stderr (and optionally a file) so that
every LangGraph node transition and every phase of the Five-Phase Write
Protocol produces a machine-parseable audit trail.

TRACE EVENT SCHEMA  (one JSON object per line):
  {
    "ts":       "2026-04-18T10:22:01.123Z",   // ISO-8601 UTC
    "job_id":   "job_a3f9",                   // random per server.py request
    "phase":    "router:call_cli",            // dot-separated namespace
    "status":   "start" | "ok" | "error" | "skipped",
    "duration_ms": 142,                       // only on ok/error
    "detail":   { ... }                       // phase-specific payload
  }

USAGE — decorator:
  from .tracing import traced, new_job_id

  job_id = new_job_id()

  @traced("router:validate", job_id=job_id)
  async def validate_routing_input(state): ...

USAGE — context manager:
  with TraceSpan("drc:gate", job_id=job_id) as span:
      run_klayout_drc(...)
  # span.detail can be set before exiting to include payload

INSPECTION WITH jq:
  # All errors:
  cat traces.jsonl | jq 'select(.status=="error")'

  # Latency by phase:
  cat traces.jsonl | jq 'select(.status=="ok") | {phase, duration_ms}'
"""

from __future__ import annotations

import functools
import inspect
import json
import logging
import os
import random
import string
import sys
import time
from datetime import datetime, timezone
from typing import Any, Callable, Optional

logger = logging.getLogger(__name__)

# Optional file sink — set EDA_TRACE_FILE=/path/to/traces.jsonl
_TRACE_FILE = os.getenv("EDA_TRACE_FILE")
_trace_fh: Optional[Any] = None

if _TRACE_FILE:
    try:
        _trace_fh = open(_TRACE_FILE, "a", buffering=1)  # line-buffered
    except OSError as _e:
        logger.warning("Cannot open trace file %s: %s", _TRACE_FILE, _e)


def new_job_id() -> str:
    """Generate a short random job ID, e.g. 'job_a3f9'."""
    suffix = "".join(random.choices(string.ascii_lowercase + string.digits, k=4))
    return f"job_{suffix}"


def _emit(event: dict[str, Any]) -> None:
    """Write one trace event as a JSON line to stderr (and file if configured)."""
    line = json.dumps(event, separators=(",", ":"))
    print(line, file=sys.stderr)
    if _trace_fh:
        print(line, file=_trace_fh)


def _now_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


# ─── TraceSpan context manager ────────────────────────────────────────────────
class TraceSpan:
    """
    Context manager that emits a 'start' event on entry and 'ok'/'error' on exit.

    with TraceSpan("drc:gate", job_id=job_id) as span:
        span.detail["shapes"] = 12000
        run_klayout()
    """

    def __init__(self, phase: str, *, job_id: str = "") -> None:
        self.phase   = phase
        self.job_id  = job_id
        self.detail: dict[str, Any] = {}
        self._t0     = 0.0

    def __enter__(self) -> "TraceSpan":
        self._t0 = time.perf_counter()
        _emit({
            "ts":     _now_iso(),
            "job_id": self.job_id,
            "phase":  self.phase,
            "status": "start",
            "detail": {},
        })
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> bool:
        duration_ms = round((time.perf_counter() - self._t0) * 1000)
        if exc_type is None:
            _emit({
                "ts":          _now_iso(),
                "job_id":      self.job_id,
                "phase":       self.phase,
                "status":      "ok",
                "duration_ms": duration_ms,
                "detail":      self.detail,
            })
        else:
            _emit({
                "ts":          _now_iso(),
                "job_id":      self.job_id,
                "phase":       self.phase,
                "status":      "error",
                "duration_ms": duration_ms,
                "detail":      {**self.detail, "error": str(exc_val)},
            })
        return False  # do not suppress the exception


# ─── @traced decorator ────────────────────────────────────────────────────────
def traced(phase: str, *, job_id: str = "") -> Callable:
    """
    Decorator for both sync and async functions.  Emits start/ok/error events.

    @traced("router:call_cli", job_id=some_job_id)
    async def call_router_via_cli(state): ...

    If the decorated function is a LangGraph node, pass job_id from state:
    The decorator reads state["job_id"] if job_id arg is "".
    """
    def decorator(fn: Callable) -> Callable:
        if inspect.iscoroutinefunction(fn):
            @functools.wraps(fn)
            async def async_wrapper(*args, **kwargs):
                # Try to pull job_id from the first arg (LangGraph state dict)
                _jid = job_id
                if not _jid and args and isinstance(args[0], dict):
                    _jid = args[0].get("job_id", "")
                t0 = time.perf_counter()
                _emit({"ts": _now_iso(), "job_id": _jid, "phase": phase, "status": "start", "detail": {}})
                try:
                    result = await fn(*args, **kwargs)
                    duration_ms = round((time.perf_counter() - t0) * 1000)
                    _emit({"ts": _now_iso(), "job_id": _jid, "phase": phase,
                           "status": "ok", "duration_ms": duration_ms, "detail": {}})
                    return result
                except Exception as e:
                    duration_ms = round((time.perf_counter() - t0) * 1000)
                    _emit({"ts": _now_iso(), "job_id": _jid, "phase": phase,
                           "status": "error", "duration_ms": duration_ms,
                           "detail": {"error": str(e)}})
                    raise
            return async_wrapper
        else:
            @functools.wraps(fn)
            def sync_wrapper(*args, **kwargs):
                _jid = job_id
                if not _jid and args and isinstance(args[0], dict):
                    _jid = args[0].get("job_id", "")
                t0 = time.perf_counter()
                _emit({"ts": _now_iso(), "job_id": _jid, "phase": phase, "status": "start", "detail": {}})
                try:
                    result = fn(*args, **kwargs)
                    duration_ms = round((time.perf_counter() - t0) * 1000)
                    _emit({"ts": _now_iso(), "job_id": _jid, "phase": phase,
                           "status": "ok", "duration_ms": duration_ms, "detail": {}})
                    return result
                except Exception as e:
                    duration_ms = round((time.perf_counter() - t0) * 1000)
                    _emit({"ts": _now_iso(), "job_id": _jid, "phase": phase,
                           "status": "error", "duration_ms": duration_ms,
                           "detail": {"error": str(e)}})
                    raise
            return sync_wrapper
    return decorator
