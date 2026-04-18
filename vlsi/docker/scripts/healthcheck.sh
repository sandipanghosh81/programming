#!/bin/bash
# healthcheck.sh  —  Check health of all VLSI agent services
# ─────────────────────────────────────────────────────────────────────────────
# Usage:
#   ./docker/scripts/healthcheck.sh
#
# Output: table of service → status

set -uo pipefail

AGENT_URL="${AGENT_URL:-http://localhost:8000}"
CHROMADB_URL="http://localhost:8001"
KLAYOUT_URL="http://localhost:8002"
DAEMON_HOST="localhost"
DAEMON_PORT="8080"

check_http() {
    local name="$1"
    local url="$2"
    local endpoint="${3:-/health}"
    if curl -sf --max-time 3 "${url}${endpoint}" > /dev/null 2>&1; then
        printf "  %-20s  \033[32m✓ OK\033[0m\n" "${name}"
    else
        printf "  %-20s  \033[31m✗ UNREACHABLE\033[0m\n" "${name}"
    fi
}

check_tcp() {
    local name="$1"
    local host="$2"
    local port="$3"
    if nc -z -w3 "${host}" "${port}" 2>/dev/null; then
        printf "  %-20s  \033[32m✓ OK\033[0m\n" "${name}"
    else
        printf "  %-20s  \033[31m✗ UNREACHABLE\033[0m\n" "${name}"
    fi
}

echo "=== VLSI Agent Health Check ==="
echo ""
check_http "Agent (Python)"     "${AGENT_URL}"
check_http "ChromaDB"           "${CHROMADB_URL}" "/api/v1/heartbeat"
check_http "KLayout DRC"        "${KLAYOUT_URL}"
check_tcp  "EDA Daemon (C++)"   "${DAEMON_HOST}" "${DAEMON_PORT}"
check_tcp  "HEA Port 9090"      "localhost" "9090"
echo ""
echo "  noVNC debug viewer: http://localhost:6080/vnc.html"
echo "  Agent API docs:     ${AGENT_URL}/docs"
echo "  Trace log:          /eda_share/traces.jsonl"
echo ""
