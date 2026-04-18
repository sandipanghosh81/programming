"""
script_synthesis.py  —  AI-assisted EU script generation
─────────────────────────────────────────────────────────────────────────────
Used when no static EU template exists for a requested operation.
Combines:
  1. RAG retrieval: pull relevant API doc chunks from ChromaDB
  2. LLM generation: generate SKILL/Tcl/Python script from those chunks
  3. REPL validation: dry-run the generated script in the Python REPL sandbox
     (KLayout-Python scripts only — SKILL/Tcl cannot be sandboxed here)

The result is an unregistered (dynamic) EU string.  If the script passes
REPL validation it is returned to the caller.  If not, the error is fed
back to the LLM for one correction attempt.

INTEGRATION POINT:
  Called from eu_compiler.compile_eu() when template lookup fails AND
  EDA_ALLOW_SYNTHESIS=true (default: false — requires explicit opt-in).
"""

from __future__ import annotations

import logging
import os
from typing import Any

logger = logging.getLogger(__name__)

SYNTHESIS_ENABLED = os.getenv("EDA_ALLOW_SYNTHESIS", "false").lower() == "true"
MAX_RETRY_ATTEMPTS = int(os.getenv("EDA_SYNTHESIS_RETRIES", "1"))


async def synthesize_eu(
    host: str,
    operation: str,
    args: dict[str, Any],
    description: str,
) -> str | None:
    """
    Synthesize an EU script using RAG + LLM.

    Args:
        host:        Target EDA host ("cadence_virtuoso", "synopsys_icc2", "klayout")
        operation:   Human-readable operation (e.g. "route net VDD on M2")
        args:        Template variables that must appear in the generated script
        description: Free-text description for RAG query

    Returns:
        Generated script string, or None if synthesis is disabled or fails.
    """
    if not SYNTHESIS_ENABLED:
        logger.debug("[Synthesis] Disabled — set EDA_ALLOW_SYNTHESIS=true to enable")
        return None

    # ── 1. RAG context retrieval ───────────────────────────────────────────────
    rag_context = await _fetch_rag_context(description, host)

    # ── 2. LLM generation ─────────────────────────────────────────────────────
    lang_map = {"cadence_virtuoso": "SKILL", "synopsys_icc2": "Tcl", "klayout": "Python"}
    lang = lang_map.get(host, "Python")

    for attempt in range(MAX_RETRY_ATTEMPTS + 1):
        script = await _generate_script(lang, operation, args, rag_context)
        if not script:
            return None

        # ── 3. REPL validation (KLayout Python only) ──────────────────────────
        if lang == "Python":
            from .python_repl import run_in_repl
            ok, error = await run_in_repl(script, timeout_s=10.0)
            if ok:
                logger.info("[Synthesis] Script validated in REPL (attempt %d)", attempt + 1)
                return script
            if attempt < MAX_RETRY_ATTEMPTS:
                logger.warning("[Synthesis] REPL validation failed (attempt %d): %s — retrying", attempt + 1, error)
                rag_context += f"\n\nPREVIOUS ATTEMPT FAILED:\n{error}\nPlease fix the script."
        else:
            # Cannot sandbox SKILL/Tcl — return as-is with warning
            logger.warning("[Synthesis] Cannot sandbox %s script — returning unvalidated", lang)
            return script

    return None


async def _fetch_rag_context(description: str, host: str) -> str:
    """Pull top-3 API doc chunks from ChromaDB for the given description."""
    try:
        from .rag.rag_service import get_rag_client
        client     = get_rag_client()
        collection = client.get_or_create_collection("eda_tool_docs")
        results    = collection.query(
            query_texts=[description],
            n_results=3,
            where={"host": host} if host else None,
            include=["documents"],
        )
        chunks = results.get("documents", [[]])[0]
        return "\n\n---\n\n".join(chunks) if chunks else ""
    except Exception as e:
        logger.debug("[Synthesis] RAG context fetch failed: %s", e)
        return ""


async def _generate_script(
    lang: str,
    operation: str,
    args: dict[str, Any],
    context: str,
) -> str | None:
    """Call the LLM to generate a script given context and args."""
    try:
        from langchain_google_genai import ChatGoogleGenerativeAI
        from langchain_core.messages import HumanMessage
        from .utils.env_bootstrap import gemini_api_key

        llm = ChatGoogleGenerativeAI(
            model="gemini-2.0-flash",
            temperature=0.1,
            api_key=gemini_api_key(),
        )

        system = (
            f"You are an expert EDA scripting assistant.\n"
            f"Generate a {lang} script to perform: {operation}\n"
            f"Available variables: {list(args.keys())}\n"
            f"Use {{ variable_name }} syntax for Jinja2 template variables.\n"
            f"Return ONLY the script body, no explanation, no markdown fences.\n\n"
            f"RELEVANT API DOCUMENTATION:\n{context or '(none available)'}"
        )

        response = await llm.ainvoke([HumanMessage(content=system)])
        return response.content.strip()
    except Exception as e:
        logger.error("[Synthesis] LLM call failed: %s", e)
        return None
