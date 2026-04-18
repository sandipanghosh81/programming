#!/bin/bash
# init_rag.sh  —  Ingest EDA API docs into ChromaDB
# ─────────────────────────────────────────────────────────────────────────────
# Run AFTER docker compose is up and ChromaDB is healthy.
#
# Usage:
#   ./docker/scripts/init_rag.sh [/path/to/eda/api/docs]
#
# Default docs dir: /eda_share/docs  (mount your API docs there)

set -euo pipefail

DOCS_DIR="${1:-/eda_share/docs}"
AGENT_URL="${AGENT_URL:-http://localhost:8000}"

echo "=== VLSI Agent: RAG Initialisation ==="
echo "Docs directory: ${DOCS_DIR}"
echo "Agent URL:      ${AGENT_URL}"
echo ""

# Wait for ChromaDB to be healthy
echo "[1/3] Waiting for ChromaDB..."
for i in $(seq 1 30); do
    if curl -sf http://localhost:8001/api/v1/heartbeat > /dev/null 2>&1; then
        echo "      ChromaDB ready."
        break
    fi
    if [[ ${i} -eq 30 ]]; then
        echo "ERROR: ChromaDB did not start in time" >&2
        exit 1
    fi
    sleep 2
done

# Wait for agent to be healthy
echo "[2/3] Waiting for agent..."
for i in $(seq 1 30); do
    if curl -sf "${AGENT_URL}/health" > /dev/null 2>&1; then
        echo "      Agent ready."
        break
    fi
    sleep 2
done

# Run ingestion
echo "[3/3] Running RAG ingestor..."
if [[ -d "${DOCS_DIR}" ]]; then
    docker compose exec agent \
        python3 -m orchestrator.rag.ingestor --docs-dir "${DOCS_DIR}"
    echo "=== RAG initialisation complete ==="
else
    echo "WARNING: Docs directory '${DOCS_DIR}' does not exist."
    echo "         Create it and add EDA API docs (Markdown files), then re-run."
    echo "         Example structure:"
    echo "           /eda_share/docs/cadence_virtuoso/skill_api.md"
    echo "           /eda_share/docs/synopsys_icc2/tcl_api.md"
    echo "           /eda_share/docs/klayout/python_api.md"
fi
