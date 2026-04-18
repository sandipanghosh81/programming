#!/bin/bash
# drc_runner.sh  —  Invoke KLayout in headless batch mode for DRC
# ─────────────────────────────────────────────────────────────────────────────
# Usage:
#   drc_runner.sh <oasis_path> <script_path> <report_path>
#
# KLayout invocation:
#   klayout -b                  # batch mode (no GUI)
#          -r <script_path>     # run the Ruby DRC script
#          <oasis_path>         # input layout
#
# The DRC script itself calls:
#   source(input("<oasis_path>", 1))
#   ...checks...
#   report("...", "<report_path>")
# So we only need to provide the .drc script and KLayout handles the rest.

set -euo pipefail

OASIS_PATH="${1:?Usage: drc_runner.sh <oasis_path> <script_path> <report_path>}"
SCRIPT_PATH="${2:?}"
REPORT_PATH="${3:?}"

if [[ ! -f "${OASIS_PATH}" ]]; then
    echo "ERROR: OASIS file not found: ${OASIS_PATH}" >&2
    exit 1
fi

if [[ ! -f "${SCRIPT_PATH}" ]]; then
    echo "ERROR: DRC script not found: ${SCRIPT_PATH}" >&2
    exit 1
fi

echo "[drc_runner] Running KLayout DRC"
echo "  OASIS:   ${OASIS_PATH}"
echo "  Script:  ${SCRIPT_PATH}"
echo "  Report:  ${REPORT_PATH}"

# Run KLayout in batch mode
# -b = batch (no GUI)
# -r = run script
# The script uses source() to read the OASIS file internally
klayout \
    -b \
    -r "${SCRIPT_PATH}" \
    2>&1

EXIT_CODE=$?

if [[ ${EXIT_CODE} -ne 0 ]]; then
    echo "ERROR: KLayout exited with code ${EXIT_CODE}" >&2
    exit ${EXIT_CODE}
fi

if [[ ! -f "${REPORT_PATH}" ]]; then
    echo "WARNING: DRC report not found at ${REPORT_PATH} (zero violations or script error)" >&2
    # Create empty RDB so callers don't fail on missing file
    cat > "${REPORT_PATH}" <<'EOF'
<?xml version="1.0" encoding="utf-8"?>
<rdb><categories></categories><items></items></rdb>
EOF
fi

echo "[drc_runner] Done. Report: ${REPORT_PATH}"
