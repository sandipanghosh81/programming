"""
embeddings.py  —  Embedding function for RAG ingestion and queries
─────────────────────────────────────────────────────────────────────────────
Wraps the sentence-transformers model used to embed EDA API doc chunks.

MODEL CHOICE (all-MiniLM-L6-v2):
  - 22M params, ~80 MB download, runs on CPU without GPU
  - 384-dimensional embeddings — adequate for technical doc retrieval
  - sub-50ms per batch of 32 chunks on modern laptop CPU
  - Matches what ChromaDB's default embedding function uses, so we can also
    let ChromaDB call it directly via chromadb.utils.embedding_functions

CONSISTENCY GUARANTEE:
  The ingestor uses the same model as the query path.  Mixing models between
  ingest and query silently degrades recall — avoid changing this without
  re-ingesting all documents.
"""

from __future__ import annotations

import logging
import os
from typing import Any

logger = logging.getLogger(__name__)

_MODEL_NAME = os.getenv("EDA_EMBEDDING_MODEL", "all-MiniLM-L6-v2")
_model: Any = None


def get_embedding_function() -> Any:
    """
    Return a ChromaDB-compatible embedding function.

    Uses chromadb.utils.embedding_functions.SentenceTransformerEmbeddingFunction
    so it can be passed directly to collection.create() / collection.query().
    """
    try:
        from chromadb.utils.embedding_functions import SentenceTransformerEmbeddingFunction
        return SentenceTransformerEmbeddingFunction(model_name=_MODEL_NAME)
    except ImportError:
        pass

    # Fallback: use sentence_transformers directly
    try:
        from sentence_transformers import SentenceTransformer

        global _model
        if _model is None:
            logger.info("[Embeddings] Loading model %s ...", _MODEL_NAME)
            _model = SentenceTransformer(_MODEL_NAME)

        class _DirectEmbedFn:
            def __call__(self, input: list[str]) -> list[list[float]]:
                embeddings = _model.encode(input, normalize_embeddings=True)
                return embeddings.tolist()

        return _DirectEmbedFn()

    except ImportError as e:
        raise RuntimeError(
            "Neither chromadb.utils.embedding_functions nor sentence_transformers "
            "is available.  Install: pip install sentence-transformers"
        ) from e


def embed_texts(texts: list[str]) -> list[list[float]]:
    """Convenience wrapper: embed a list of strings and return float vectors."""
    fn = get_embedding_function()
    return fn(texts)
