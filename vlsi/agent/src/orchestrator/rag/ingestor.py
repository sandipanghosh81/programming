"""
ingestor.py  —  EDA API documentation ingestor for ChromaDB
─────────────────────────────────────────────────────────────────────────────
Reads Markdown / plain-text EDA API docs from a directory tree and upserts
chunks into the ChromaDB "eda_tool_docs" collection.

CHUNKING STRATEGY:
  Documents are split at `## ` and `### ` headings (semantic sections).
  Each chunk is at most 800 tokens (≈ 600 words).  Overlap is NOT used;
  sections rarely need context from adjacent headings in EDA API docs.

METADATA INFERRED FROM DIRECTORY LAYOUT:
  docs/
    cadence_virtuoso/    → host="cadence_virtuoso", language="skill"
    synopsys_icc2/       → host="synopsys_icc2",    language="tcl"
    klayout/             → host="klayout",          language="python"
    general/             → host="general",          language="mixed"

  The template field is left blank at ingest time; it is enriched by the
  policy layer when a RAG hit is used to select a template.

USAGE (CLI):
  python -m orchestrator.rag.ingestor --docs-dir /eda_share/docs

USAGE (programmatic):
  from orchestrator.rag.ingestor import ingest_directory
  ingest_directory("/eda_share/docs")
"""

from __future__ import annotations

import argparse
import logging
import os
import re
from pathlib import Path
from typing import Iterator

logger = logging.getLogger(__name__)

MAX_CHUNK_CHARS = 3200   # ~800 tokens at 4 chars/token


def _split_into_sections(text: str) -> list[tuple[str, str]]:
    """
    Split a Markdown document at ## / ### headings.
    Returns list of (heading, content) pairs.
    """
    # Match lines starting with ## or ### (but not ####)
    pattern = re.compile(r'^(#{2,3} .+)$', re.MULTILINE)
    positions = [(m.start(), m.group(1)) for m in pattern.finditer(text)]

    if not positions:
        return [("", text)]

    sections = []
    for i, (pos, heading) in enumerate(positions):
        start = pos + len(heading) + 1   # skip past the heading line
        end   = positions[i + 1][0] if i + 1 < len(positions) else len(text)
        content = text[start:end].strip()
        if content:
            sections.append((heading.lstrip("# ").strip(), content))

    return sections


def _chunk_text(text: str, max_chars: int = MAX_CHUNK_CHARS) -> Iterator[str]:
    """Split text into chunks of at most max_chars characters."""
    while len(text) > max_chars:
        split_at = text.rfind("\n", 0, max_chars)
        if split_at < 0:
            split_at = max_chars
        yield text[:split_at].strip()
        text = text[split_at:].strip()
    if text:
        yield text


_HOST_MAP: dict[str, tuple[str, str]] = {
    "cadence_virtuoso": ("cadence_virtuoso", "skill"),
    "synopsys_icc2":    ("synopsys_icc2",    "tcl"),
    "klayout":          ("klayout",          "python"),
    "general":          ("general",          "mixed"),
}


def ingest_directory(docs_root: str | Path, dry_run: bool = False) -> int:
    """
    Walk docs_root and upsert all .md / .txt files into ChromaDB.

    Returns:
        Number of chunks upserted.
    """
    from .rag_service import get_rag_client
    from .embeddings import get_embedding_function

    docs_root = Path(docs_root)
    if not docs_root.is_dir():
        raise FileNotFoundError(f"docs_root does not exist: {docs_root}")

    client     = get_rag_client()
    embed_fn   = get_embedding_function()
    collection = client.get_or_create_collection(
        "eda_tool_docs",
        embedding_function=embed_fn,
    )

    ids, documents, metadatas = [], [], []
    total_chunks = 0

    for md_file in docs_root.rglob("*.md"):
        host, lang = _infer_host(md_file, docs_root)
        text = md_file.read_text(encoding="utf-8", errors="replace")
        sections = _split_into_sections(text)

        for heading, content in sections:
            for chunk_idx, chunk in enumerate(_chunk_text(content)):
                chunk_id = f"{md_file.stem}:{heading}:{chunk_idx}"
                ids.append(chunk_id)
                documents.append(f"{heading}\n\n{chunk}" if heading else chunk)
                metadatas.append({
                    "host":     host,
                    "language": lang,
                    "template": "",  # enriched later by policy layer
                    "source":   md_file.name,
                    "section":  heading,
                })
                total_chunks += 1

                # Batch upsert every 64 chunks
                if len(ids) >= 64:
                    if not dry_run:
                        collection.upsert(ids=ids, documents=documents, metadatas=metadatas)
                        logger.info("[Ingestor] Upserted %d chunks so far", total_chunks)
                    ids, documents, metadatas = [], [], []

    # Flush remainder
    if ids and not dry_run:
        collection.upsert(ids=ids, documents=documents, metadatas=metadatas)

    logger.info("[Ingestor] Done — %d total chunks from %s", total_chunks, docs_root)
    return total_chunks


def _infer_host(path: Path, root: Path) -> tuple[str, str]:
    """Infer host/language from the directory name relative to root."""
    parts = path.relative_to(root).parts
    if parts:
        dir_name = parts[0].lower()
        for key, value in _HOST_MAP.items():
            if dir_name.startswith(key.split("_")[0]):
                return value
    return ("general", "mixed")


# ── CLI entry point ───────────────────────────────────────────────────────────
if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    parser = argparse.ArgumentParser(description="Ingest EDA API docs into ChromaDB")
    parser.add_argument("--docs-dir", required=True, help="Root directory of API docs")
    parser.add_argument("--dry-run",  action="store_true", help="Parse only, don't write")
    args = parser.parse_args()
    n = ingest_directory(args.docs_dir, dry_run=args.dry_run)
    print(f"Done: {n} chunks {'parsed (dry-run)' if args.dry_run else 'upserted'}")
