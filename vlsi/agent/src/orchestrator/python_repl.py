"""
python_repl.py  —  Isolated Python REPL sandbox for EU dry-run
─────────────────────────────────────────────────────────────────────────────
Runs generated KLayout-Python scripts in a subprocess with:
  - A KLayout API stub (so klayout.db is importable without KLayout installed)
  - A strict timeout to prevent runaway scripts
  - stdout/stderr capture for error reporting

SECURITY NOTE:
  This sandbox is not a security boundary — it only validates syntax and
  basic API call correctness.  The subprocess runs with the same OS user
  as the agent.  Do NOT run untrusted scripts here.

USAGE:
  from orchestrator.python_repl import run_in_repl

  ok, error = await run_in_repl(script, timeout_s=10.0)
  if not ok:
      print("Script failed:", error)
"""

from __future__ import annotations

import asyncio
import logging
import os
import sys
import tempfile
import textwrap
from pathlib import Path

logger = logging.getLogger(__name__)

# Path to the KLayout stub module (sits alongside this file in rag/stubs/)
_STUB_DIR = Path(__file__).parent / "rag" / "stubs"

# Wrapper that injects the stub directory into sys.path before the user script
_WRAPPER_TEMPLATE = textwrap.dedent("""\
import sys
sys.path.insert(0, {stub_dir!r})
# ── User script below ─────────────────────────────────────────────────────
{script}
""")


async def run_in_repl(
    script: str,
    timeout_s: float = 10.0,
    extra_sys_path: list[str] | None = None,
) -> tuple[bool, str]:
    """
    Execute script in an isolated subprocess.

    Returns:
        (True, "")          on success
        (False, error_text) on syntax error, runtime error, or timeout
    """
    stub_dir = str(_STUB_DIR)
    wrapper  = _WRAPPER_TEMPLATE.format(stub_dir=stub_dir, script=script)

    extra_paths = extra_sys_path or []

    with tempfile.NamedTemporaryFile(suffix=".py", mode="w",
                                     delete=False, encoding="utf-8") as f:
        f.write(wrapper)
        tmp_path = f.name

    try:
        env = os.environ.copy()
        if extra_paths:
            env["PYTHONPATH"] = os.pathsep.join(extra_paths + [env.get("PYTHONPATH", "")])

        proc = await asyncio.create_subprocess_exec(
            sys.executable, tmp_path,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
            env=env,
        )

        try:
            stdout, stderr = await asyncio.wait_for(
                proc.communicate(), timeout=timeout_s
            )
        except asyncio.TimeoutError:
            proc.kill()
            await proc.communicate()
            return False, f"Script timed out after {timeout_s:.0f}s"

        if proc.returncode != 0:
            error = stderr.decode(errors="replace").strip()
            return False, error or f"Script exited with code {proc.returncode}"

        return True, ""

    finally:
        try:
            os.unlink(tmp_path)
        except OSError:
            pass
