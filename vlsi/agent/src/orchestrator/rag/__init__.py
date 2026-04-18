"""
rag/  —  Retrieval-Augmented Generation memory service
─────────────────────────────────────────────────────────────────────────────
Provides access to the ChromaDB vector store containing EDA tool API docs.

Public surface:
  from orchestrator.rag import get_rag_client, ingest_docs, search
"""

from .rag_service import get_rag_client
from .ingestor import ingest_directory

__all__ = ["get_rag_client", "ingest_directory"]
