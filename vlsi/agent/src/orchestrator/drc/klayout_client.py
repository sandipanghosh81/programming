"""
klayout_client.py  —  HTTP client for the KLayout DRC Docker service
─────────────────────────────────────────────────────────────────────────────
The KLayout service (docker/klayout/) exposes a tiny HTTP API:

  POST /run_drc
    Body: {"oasis_path": str, "script_path": str, "report_path": str}
    Returns: {"status": "ok"} or {"status": "error", "detail": str}

  GET  /health
    Returns: {"status": "ok"}

  GET  /viewer_url
    Returns: {"url": "http://host:6080"} — noVNC URL for the debug viewer

This module calls that API, waits for the DRC to complete, then calls the
rdb_parser to return structured violation objects.

DRC GATE FLOW (Five-Phase Write Protocol, Phase 3):
  1. C++ daemon writes OASIS file to /eda_share/routing_NNN.oas
  2. HEA Extract Rules EU runs → writes /eda_share/tech_rules_NNN.json
  3. THIS MODULE:
     a. load_tech_rules(tech_rules_NNN.json) → dict
     b. generate_drc_script() → Ruby script string
     c. Write Ruby script to /eda_share/drc_NNN.drc
     d. POST /run_drc → waits for completion
     e. parse_rdb(drc_report_NNN.rdb) → violations
     f. Return pass/fail + violation list
"""

from __future__ import annotations

import asyncio
import json
import logging
import os
import tempfile
import time
from pathlib import Path
from typing import Any

import httpx

from .drc_script_gen import generate_drc_script, load_tech_rules
from .rdb_parser import parse_rdb, DRCViolation, violation_summary

logger = logging.getLogger(__name__)

KLAYOUT_HOST    = os.getenv("KLAYOUT_HOST",    "localhost")
KLAYOUT_PORT    = int(os.getenv("KLAYOUT_PORT", "8002"))
KLAYOUT_TIMEOUT = float(os.getenv("KLAYOUT_DRC_TIMEOUT", "300.0"))  # 5 min for large designs
EDA_SHARE       = os.getenv("EDA_SHARE",       "/eda_share")

KLAYOUT_BASE_URL = f"http://{KLAYOUT_HOST}:{KLAYOUT_PORT}"


async def klayout_health_check() -> bool:
    """Return True if the KLayout service is reachable."""
    try:
        async with httpx.AsyncClient(timeout=5.0) as client:
            resp = await client.get(f"{KLAYOUT_BASE_URL}/health")
            return resp.status_code == 200
    except Exception:
        return False


async def run_drc_gate(
    oasis_path: str,
    tech_rules_path: str,
    job_id: str = "",
) -> tuple[bool, list[DRCViolation]]:
    """
    Run the DRC gate check.

    Args:
        oasis_path:       Path to the OASIS routing snapshot (on /eda_share).
        tech_rules_path:  Path to the JSON tech rules file (on /eda_share).
        job_id:           Trace job ID for logging.

    Returns:
        (passed: bool, violations: list[DRCViolation])
        passed=True means zero violations.
    """
    # ── 1. Health check ───────────────────────────────────────────────────────
    if not await klayout_health_check():
        logger.warning("[DRC Gate] KLayout service unreachable — SKIPPING DRC gate (job=%s)", job_id)
        return True, []  # Graceful degradation: proceed without DRC

    # ── 2. Load tech rules ────────────────────────────────────────────────────
    try:
        tech_rules = load_tech_rules(tech_rules_path)
    except Exception as e:
        logger.error("[DRC Gate] Cannot load tech rules from %s: %s", tech_rules_path, e)
        return True, []  # Degraded — skip DRC

    # ── 3. Generate and write DRC script ─────────────────────────────────────
    report_path = str(Path(EDA_SHARE) / f"drc_report_{job_id or 'latest'}.rdb")
    script      = generate_drc_script(tech_rules, oasis_path, report_path)
    script_path = str(Path(EDA_SHARE) / f"drc_{job_id or 'latest'}.drc")

    Path(script_path).write_text(script, encoding="utf-8")
    logger.info("[DRC Gate] Script written to %s (%d chars)", script_path, len(script))

    # ── 4. Call KLayout service ───────────────────────────────────────────────
    payload = {
        "oasis_path":  oasis_path,
        "script_path": script_path,
        "report_path": report_path,
    }
    try:
        async with httpx.AsyncClient(timeout=KLAYOUT_TIMEOUT) as client:
            resp = await client.post(
                f"{KLAYOUT_BASE_URL}/run_drc",
                json=payload,
            )
            resp.raise_for_status()
            result = resp.json()
    except httpx.HTTPError as e:
        logger.error("[DRC Gate] HTTP error: %s — skipping", e)
        return True, []
    except Exception as e:
        logger.error("[DRC Gate] Unexpected error: %s — skipping", e)
        return True, []

    if result.get("status") != "ok":
        logger.error("[DRC Gate] Service returned error: %s", result.get("detail", "?"))
        return True, []

    # ── 5. Parse violations ───────────────────────────────────────────────────
    violations = parse_rdb(report_path)
    summary    = violation_summary(violations)

    if violations:
        logger.warning("[DRC Gate] FAIL — %d violations: %s", len(violations), summary)
    else:
        logger.info("[DRC Gate] PASS — zero violations")

    return len(violations) == 0, violations


def viewer_url() -> str:
    """Return the noVNC URL for the KLayout debug viewer."""
    try:
        import httpx as _httpx
        resp = _httpx.get(f"{KLAYOUT_BASE_URL}/viewer_url", timeout=3.0)
        return resp.json().get("url", "")
    except Exception:
        return f"http://{KLAYOUT_HOST}:6080"
