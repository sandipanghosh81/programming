# Context Memo — RAG + Docker Architecture Update
**Date:** 2026-04-18  
**Target files:** `vlsi/docs/architecture.md`, `vlsi/docs/implementation.md`  
**Status:** IN PROGRESS — refer to todo list in conversation

---

## What is being added

### 1. RAG Memory System (ChromaDB)
- **Source data:** Official EDA vendor API docs — Cadence SKILL (Virtuoso, Custom Compiler),
  Synopsys Tcl (ICC2), KLayout Python (`pya`), plus any curated Q&A or run examples
- **Use 1 — Tool selection:** Policy router queries RAG to decide which tool is applicable for
  the design (e.g., which router: standard cell vs device vs chip assembly vs memory)
- **Use 2 — Script synthesis context:** EU Compiler queries RAG for API reference before
  asking LLM to write a novel SKILL/Tcl/Python script
- **Use 3 — User query answering:** chat_node can pull API doc excerpts to answer
  "how do I do X in Virtuoso?" questions
- **Initialization:** Pre-built ChromaDB embeddings bundled in Docker image (sourced from
  vendor docs at image build time); can be re-indexed on version update
- **Update trigger:** Admin command (`docker exec agent python -m rag.update --version <ver>`)
  or optional periodic scheduler

### 2. Script Synthesis Node (user's "pythonREPL" — option C: both)
- **A — Script Synthesis:** RAG retrieval → LLM synthesizes SKILL/Tcl/Python with API context
  → static validation → EU Compiler → deploy to HEA
- **B — Python REPL sandbox:** Actual `PythonREPL` tool (LangChain-style) inside Docker for
  dry-running Python EUs before deploying to host HEA. For SKILL/Tcl: syntax-only validate
  (can't execute natively in Docker, but can use mock stub if provided)
- Lives in: `vlsi/agent/src/orchestrator/script_synthesis.py`

### 3. Docker Container (ships with tool installation)
**Services in docker-compose.yml:**
- `agent` — FastAPI + LangGraph + RAG (port 8000)
- `chromadb` — ChromaDB HTTP server (port 8001, internal network only)
- `eda_daemon` — C++ binary compiled for linux/amd64 (port 8080)
- `ollama` — optional local LLM service (port 11434) — see LLM provider note

**LLM Provider:** Configurable via env vars — options:
- Commercial cloud: `LLM_PROVIDER=openai`, `OPENAI_API_KEY=...`
- Local Ollama: `LLM_PROVIDER=ollama`, `OLLAMA_BASE_URL=http://ollama:11434`
- NIM or custom: `LLM_PROVIDER=openai_compatible`, `LLM_BASE_URL=...`

**Networking:**
- Docker container talks to HEA on host via `host.docker.internal:18082` (or configured IP)
- Host dock talks into Docker via mapped port `localhost:8000`

**EDA vendor docs volume:**
- `/eda_docs/` mounted at container start — RAG ingests from here on first run / on update command

---

## ChromaDB limitations to document
1. Single-node only — no clustering/HA in open-source edition
2. No built-in authentication — use reverse proxy (nginx) if exposed beyond localhost compose network
3. Must persist data via Docker named volume (`chroma_data`) — data lost on container destroy otherwise
4. Performance degrades above ~1M embeddings per collection; partition by EDA tool and version
   (e.g., collection name: `virtuoso_icadvm_23_1`)
5. No built-in collection versioning — use naming convention `<tool>_<version_slug>`

---

## Changes planned in architecture.md

| Section | Change |
|---------|--------|
| §2 At a Glance | New Mermaid flowchart with Docker boundary, RAG, Script Synthesis |
| §3 Components | New subsections 3.4 (RAG) and 3.5 (Docker); component diagram updated |
| §4.1 Capability matrix | Add RAG-assisted tool selection row |
| §5 Request Flow | Add RAG retrieval + Script Synthesis + REPL dry-run steps to sequence diagram |
| §10.1 Pattern overview | Add RAG + REPL nodes to EU pattern diagram |
| §10.4 EU Compiler pseudocode | Add RAG retrieval step before LLM synthesis; add REPL dry-run |
| §10.7 Security | Note on LLM provider configuration in Docker |
| New §12 | RAG Memory System (full section) |
| New §13 | Docker Deployment (full section) |
| §11.1 File map | Add `rag/`, `docker/`, `docker-compose.yml` |
| §11.4 Glossary | Add RAG, ChromaDB, Script Synthesis, PythonREPL, LLM Provider |

## Changes planned in implementation.md
| Section | Change |
|---------|--------|
| §1 Scope/Requirements | Note eda_daemon now runs as Docker service |
| §12 Build System | Add Docker build target |

---

## Stage completion tracking
- [x] Memo written
- [ ] Stage 1: §3 Components (new subsections 3.4, 3.5 + diagram)
- [ ] Stage 2: §2 At a Glance + §5 Request Flow
- [ ] Stage 3: §10 EU Compiler update
- [ ] Stage 4: New §12 RAG Memory System
- [ ] Stage 5: New §13 Docker Deployment
- [ ] Stage 6: §11 file map + glossary
- [ ] Stage 7: implementation.md
- [ ] Stage 8: git commit
