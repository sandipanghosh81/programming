"""
rag_service.py  —  ChromaDB client singleton
─────────────────────────────────────────────────────────────────────────────
Manages a single ChromaDB HTTP client for the entire agent process.
The ChromaDB server runs as a separate Docker service (see docker-compose.yml).

GRACEFUL DEGRADATION:
  If ChromaDB is unreachable, get_rag_client() raises RuntimeError.
  All callers (layer_router, script_synthesis) catch this and fall back to
  the static tool map.  The server never hard-fails due to RAG unavailability.

SESSION CACHING:
  RAG results are cached in-memory for 60s using a simple dict.
  This avoids redundant ChromaDB queries for the same user message within
  a single conversation turn.

COLLECTION LAYOUT:
  Collection: "eda_tool_docs"
  Metadata fields per chunk:
    host:     "cadence_virtuoso" | "synopsys_icc2" | "klayout" | "general"
    language: "skill" | "tcl" | "python"
    template: template basename this chunk is most relevant to
    source:   original doc filename
    section:  section heading from the source doc
"""

from __future__ import annotations

import hashlib
import logging
import os
import time
from typing import Any

logger = logging.getLogger(__name__)

CHROMADB_HOST = os.getenv("CHROMADB_HOST", "localhost")
CHROMADB_PORT = int(os.getenv("CHROMADB_PORT", "8001"))

_client: Any = None
_cache: dict[str, tuple[float, Any]] = {}   # key → (timestamp, result)
_CACHE_TTL_S = 60.0


def get_rag_client() -> Any:
    """
    Return (or create) the singleton ChromaDB HTTP client.

    Raises:
        RuntimeError if chromadb is not installed or server is unreachable.
    """
    global _client
    if _client is not None:
        return _client

    try:
        import chromadb
    except ImportError as e:
        raise RuntimeError("chromadb not installed: pip install chromadb") from e

    try:
        _client = chromadb.HttpClient(host=CHROMADB_HOST, port=CHROMADB_PORT)
        _client.heartbeat()  # raises if server is unreachable
        logger.info("[RAG] Connected to ChromaDB at %s:%d", CHROMADB_HOST, CHROMADB_PORT)
    except Exception as e:
        _client = None
        raise RuntimeError(f"ChromaDB unreachable at {CHROMADB_HOST}:{CHROMADB_PORT}: {e}") from e

    return _client


def search(query: str, n_results: int = 3,
           host_filter: str | None = None) -> list[dict[str, Any]]:
    """
    Search the eda_tool_docs collection.  Returns a list of hit dicts:
      {"document": str, "metadata": dict, "distance": float}

    Results are session-cached for 60s.
    """
    cache_key = hashlib.md5(f"{query}:{n_results}:{host_filter}".encode()).hexdigest()[:16]
    now = time.monotonic()

    cached = _cache.get(cache_key)
    if cached and (now - cached[0]) < _CACHE_TTL_S:
        return cached[1]

    client = get_rag_client()
    collection = client.get_or_create_collection("eda_tool_docs")

    kwargs: dict[str, Any] = {
        "query_texts": [query],
        "n_results":   n_results,
        "include":     ["documents", "metadatas", "distances"],
    }
    if host_filter:
        kwargs["where"] = {"host": host_filter}

    results = collection.query(**kwargs)

    hits = []
    for doc, meta, dist in zip(
        results.get("documents", [[]])[0],
        results.get("metadatas",  [[]])[0],
        results.get("distances",  [[]])[0],
    ):
        hits.append({"document": doc, "metadata": meta, "distance": dist})

    _cache[cache_key] = (now, hits)
    return hits


def reset_client() -> None:
    """Force re-connect on next call (useful after ChromaDB restart)."""
    global _client
    _client = None
    _cache.clear()
