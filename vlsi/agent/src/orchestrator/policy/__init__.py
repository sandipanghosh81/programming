"""
policy/  —  EDA tool routing policy layer
─────────────────────────────────────────
Decides which EDA host (Virtuoso, ICC2, KLayout, …) should receive a
given EU (Execution Unit) based on:
  1. RAG similarity search across tool API documentation (primary)
  2. Static tool map (fallback when RAG/ChromaDB is unavailable)

Public surface:
  from orchestrator.policy import resolve_tool, ToolMatch
"""

from .layer_router import resolve_tool, ToolMatch

__all__ = ["resolve_tool", "ToolMatch"]
