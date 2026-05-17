"""
Load secrets before any ChatGoogleGenerativeAI is constructed.

Load order (later overrides earlier):
  1. /Users/sandipanghosh/programming/.env  — workspace-wide shared keys
  2. vlsi/agent/.env                         — project-specific overrides

python-dotenv defaults to override=False, so an empty GOOGLE_API_KEY in the
shell would prevent .env from taking effect. We use override=True for both
files and mirror GOOGLE_API_KEY <-> GEMINI_API_KEY.
"""

from __future__ import annotations

import logging
import os
from pathlib import Path

logger = logging.getLogger(__name__)

_AGENT_DIR = Path(__file__).resolve().parent.parent.parent.parent


def load_agent_env() -> None:
    """Idempotent: safe to call more than once."""
    try:
        from dotenv import load_dotenv
    except ImportError:
        return

    root_env = Path("/Users/sandipanghosh/programming/.env")
    if root_env.is_file():
        load_dotenv(root_env, override=True)
    else:
        logger.warning("No root .env at %s — shared keys may be missing.", root_env)

    env_path = _AGENT_DIR / ".env"
    if env_path.is_file():
        load_dotenv(env_path, override=True)
    else:
        logger.warning(
            "No .env at %s — set GOOGLE_API_KEY/GEMINI_API_KEY in the environment or add this file.",
            env_path,
        )

    key = (
        (os.environ.get("GOOGLE_API_KEY") or "").strip()
        or (os.environ.get("GEMINI_API_KEY") or "").strip()
    )
    if key:
        os.environ["GOOGLE_API_KEY"] = key
        os.environ["GEMINI_API_KEY"] = key
    else:
        logger.warning(
            "Gemini API key not set. Add GOOGLE_API_KEY or GEMINI_API_KEY to %s",
            env_path,
        )


def gemini_api_key() -> str:
    """Resolved API key for ChatGoogleGenerativeAI(..., api_key=...)."""
    load_agent_env()
    k = (os.environ.get("GOOGLE_API_KEY") or os.environ.get("GEMINI_API_KEY") or "").strip()
    if not k:
        raise RuntimeError(
            "Missing Gemini API key. Set GOOGLE_API_KEY or GEMINI_API_KEY in vlsi/agent/.env "
            "or export them in the environment."
        )
    return k
