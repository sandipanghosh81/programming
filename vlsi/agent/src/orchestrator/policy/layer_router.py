"""
layer_router.py  —  Policy: RAG-first, static-map fallback tool resolver
─────────────────────────────────────────────────────────────────────────────
PRIMARY PATH (RAG available):
  1. Embed the intent description using the same model as the RAG ingestor
  2. Query ChromaDB for the top-1 most similar API doc chunk
  3. Extract host/language/template from the chunk metadata
  4. Return ToolMatch

FALLBACK PATH (RAG unavailable or low confidence):
  1. Extract operation keyword from intent_params
  2. Look up in static_tool_map.STATIC_MAP
  3. Return ToolMatch with source="static"

CONFIDENCE THRESHOLD:
  ChromaDB returns a cosine distance in [0, 2].  We treat distance > 0.6
  as low confidence and fall back to the static map.
  (0 = identical, 2 = orthogonal — in practice EDA API docs have distance
   0.15–0.45 for related topics.)
"""

from __future__ import annotations

import logging
import os
from typing import NamedTuple, Optional

from .static_tool_map import lookup as static_lookup, ToolEntry

logger = logging.getLogger(__name__)

RAG_CONFIDENCE_THRESHOLD = float(os.getenv("RAG_CONFIDENCE_THRESHOLD", "0.6"))


class ToolMatch(NamedTuple):
    host:       str   # "cadence_virtuoso" | "synopsys_icc2" | "klayout"
    language:   str   # "skill" | "tcl" | "python"
    template:   str   # template basename
    source:     str   # "rag" | "static" | "default"
    confidence: float # 0.0–1.0 (1.0 = exact match)


DEFAULT_HOST     = os.getenv("EDA_DEFAULT_HOST",     "cadence_virtuoso")
DEFAULT_LANGUAGE = os.getenv("EDA_DEFAULT_LANGUAGE",  "skill")
DEFAULT_TEMPLATE = os.getenv("EDA_DEFAULT_TEMPLATE",  "route_nets")


def resolve_tool(intent_params: dict, description: str = "") -> ToolMatch:
    """
    Resolve which EDA host / EU template should handle an operation.

    Args:
        intent_params:  The structured params dict from parse_intent.
                        Must contain at least {"operation": "<op_name>"}.
        description:    Free-text description used for RAG embedding lookup.
                        Falls back to str(intent_params) if empty.

    Returns:
        ToolMatch with host, language, template, source, confidence.
    """
    query_text = description or str(intent_params)
    operation  = intent_params.get("operation", "")

    # ── 1. Try RAG path ───────────────────────────────────────────────────────
    match = _rag_lookup(query_text)
    if match is not None:
        return match

    # ── 2. Static map fallback ────────────────────────────────────────────────
    if operation:
        entry: Optional[ToolEntry] = static_lookup(operation)
        if entry:
            logger.info("[Policy] static map: %s → %s/%s", operation, entry.host, entry.template)
            return ToolMatch(
                host=entry.host, language=entry.language,
                template=entry.template, source="static", confidence=0.8,
            )

    # ── 3. Default ────────────────────────────────────────────────────────────
    logger.warning("[Policy] No match for '%s' — using default host %s", operation, DEFAULT_HOST)
    return ToolMatch(
        host=DEFAULT_HOST, language=DEFAULT_LANGUAGE,
        template=DEFAULT_TEMPLATE, source="default", confidence=0.0,
    )


def _rag_lookup(query_text: str) -> Optional[ToolMatch]:
    """
    Attempt a ChromaDB similarity search.  Returns None if RAG is unavailable
    or confidence is below threshold.
    """
    try:
        # Lazy import: only load chromadb if it's installed and the service is up
        import chromadb  # type: ignore
        from ..rag.rag_service import get_rag_client

        client     = get_rag_client()
        collection = client.get_or_create_collection("eda_tool_docs")
        results    = collection.query(
            query_texts=[query_text],
            n_results=1,
            include=["distances", "metadatas"],
        )

        if not results["ids"] or not results["ids"][0]:
            return None

        distance = results["distances"][0][0]
        metadata = results["metadatas"][0][0]

        if distance > RAG_CONFIDENCE_THRESHOLD:
            logger.debug("[Policy] RAG distance %.3f > threshold %.3f — fallback",
                         distance, RAG_CONFIDENCE_THRESHOLD)
            return None

        host     = metadata.get("host",     DEFAULT_HOST)
        language = metadata.get("language", DEFAULT_LANGUAGE)
        template = metadata.get("template", DEFAULT_TEMPLATE)
        confidence = max(0.0, 1.0 - distance / RAG_CONFIDENCE_THRESHOLD)

        logger.info("[Policy] RAG hit: %s/%s (distance=%.3f)", host, template, distance)
        return ToolMatch(host=host, language=language, template=template,
                         source="rag", confidence=confidence)

    except Exception as e:
        logger.debug("[Policy] RAG unavailable (%s) — falling back to static map", e)
        return None
