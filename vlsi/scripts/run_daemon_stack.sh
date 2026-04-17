#!/usr/bin/env bash
# Start full VLSI stack: constraints MCP (:18081) + FastAPI agent (:8000) + eda_daemon (:8080).
# Called by: make daemon (from vlsi/Makefile)
#
# Environment (optional):
#   VLSI_PYTHON  — explicit python binary (make daemon passes PYTHON=...)
#
# Python resolution order:
#   1) VLSI_PYTHON if set and executable
#   2) vlsi/agent/.venv/bin/python if present
#   3) python3 on PATH

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# vlsi/scripts -> repo root (parent of vlsi/)
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
AGENT_DIR="${ROOT_DIR}/vlsi/agent"
EDA_BUILD="${ROOT_DIR}/vlsi/eda_tools/eda_cli/build"
EDA_DAEMON="${EDA_BUILD}/eda_daemon"
CONSTRAINTS_MCP="${ROOT_DIR}/vlsi/eda_tools/python/constraints_tool/mcp_server.py"

AGENT_LOG="${TMPDIR:-/tmp}/vlsi-agent.log"
CONSTRAINTS_LOG="${TMPDIR:-/tmp}/vlsi-constraints.log"

resolve_python() {
	if [[ -n "${VLSI_PYTHON:-}" && -x "${VLSI_PYTHON}" ]]; then
		echo "${VLSI_PYTHON}"
		return 0
	fi
	local v="${AGENT_DIR}/.venv/bin/python"
	if [[ -x "${v}" ]]; then
		echo "${v}"
		return 0
	fi
	if command -v python3 >/dev/null 2>&1; then
		command -v python3
		return 0
	fi
	echo "[vlsi] ERROR: No usable Python. Options:" >&2
	echo "  cd vlsi/agent && python3 -m venv .venv && .venv/bin/pip install -e ." >&2
	echo "  Or:  export VLSI_PYTHON=/path/to/python3" >&2
	exit 1
}

PY="$(resolve_python)"
echo "[vlsi] using Python: ${PY}"
"$PY" -V

if ! "$PY" -c "import uvicorn, fastapi" 2>/dev/null; then
	echo "[vlsi] ERROR: Agent dependencies missing (uvicorn/fastapi)." >&2
	echo "  Fix once:" >&2
	echo "    cd ${AGENT_DIR} && ${PY} -m pip install -e ." >&2
	exit 1
fi

if [[ ! -x "${EDA_DAEMON}" ]]; then
	echo "[vlsi] ERROR: eda_daemon not built: ${EDA_DAEMON}" >&2
	echo "  Run:  cd ${ROOT_DIR}/vlsi && make build-daemon" >&2
	exit 1
fi

if [[ ! -f "${CONSTRAINTS_MCP}" ]]; then
	echo "[vlsi] ERROR: constraints MCP script missing: ${CONSTRAINTS_MCP}" >&2
	exit 1
fi

echo "[vlsi] starting constraints MCP (:18081) — log ${CONSTRAINTS_LOG}"
"$PY" "${CONSTRAINTS_MCP}" >"${CONSTRAINTS_LOG}" 2>&1 &
CONSTRAINTS_PID=$!
echo "[vlsi] constraints pid=${CONSTRAINTS_PID}"

echo "[vlsi] starting agent (:8000) — log ${AGENT_LOG}"
(
	cd "${AGENT_DIR}"
	exec "$PY" -u server.py
) >"${AGENT_LOG}" 2>&1 &
AGENT_PID=$!
echo "[vlsi] agent pid=${AGENT_PID}"

cleanup() {
	echo "[vlsi] stopping background services..."
	kill "${AGENT_PID}" "${CONSTRAINTS_PID}" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# Graph compile + uvicorn bind can take many seconds; do not use a 1s sleep only.
echo "[vlsi] waiting for agent http://127.0.0.1:8000/health ..."
ok=0
for _ in $(seq 1 120); do
	if curl -sfS --connect-timeout 1 "http://127.0.0.1:8000/health" >/dev/null 2>&1; then
		ok=1
		break
	fi
	if ! kill -0 "${AGENT_PID}" 2>/dev/null; then
		echo "[vlsi] ERROR: agent process exited before becoming healthy. Last log lines:" >&2
		tail -n 80 "${AGENT_LOG}" >&2 || true
		exit 1
	fi
	sleep 0.5
done

if [[ "${ok}" -ne 1 ]]; then
	echo "[vlsi] ERROR: agent did not respond on :8000 within ~60s." >&2
	tail -n 80 "${AGENT_LOG}" >&2 || true
	exit 1
fi

echo "[vlsi] agent health check: OK (http://127.0.0.1:8000)"
echo "[vlsi] starting eda_daemon (:8080) — foreground; Ctrl+C stops all services"
# Do not `exec` here: we need this shell to survive so EXIT trap kills agent + constraints.
"${EDA_DAEMON}"
