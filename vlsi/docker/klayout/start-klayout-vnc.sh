#!/bin/bash
# start-klayout-vnc.sh  —  Entrypoint for the KLayout DRC + noVNC service
# ─────────────────────────────────────────────────────────────────────────────
# Sequence:
#   1. Start Xvfb virtual display on :99
#   2. Start x11vnc → forwards X11 to VNC on :5900
#   3. Start noVNC websockify → serves browser VNC on :6080
#   4. Start KLayout in GUI mode (background, uses the virtual display)
#   5. Start Python HTTP DRC server on :8002 (foreground)

set -e

DISPLAY_NUM=":99"
VNC_PORT=5900
NOVNC_PORT=6080
DRC_PORT=8002

echo "[klayout-service] Starting Xvfb on ${DISPLAY_NUM} ..."
Xvfb "${DISPLAY_NUM}" -screen 0 1280x1024x24 -ac &
export DISPLAY="${DISPLAY_NUM}"
sleep 1

echo "[klayout-service] Starting x11vnc on port ${VNC_PORT} ..."
x11vnc -display "${DISPLAY_NUM}" -nopw -forever -quiet -rfbport "${VNC_PORT}" &
sleep 1

echo "[klayout-service] Starting noVNC on port ${NOVNC_PORT} ..."
websockify --web /usr/share/novnc/ "${NOVNC_PORT}" localhost:"${VNC_PORT}" &

echo "[klayout-service] Starting KLayout in GUI mode ..."
klayout -e &   # -e = edit mode, allows scripting; runs in background on Xvfb
sleep 2

echo "[klayout-service] Starting DRC HTTP server on port ${DRC_PORT} ..."
exec python3 /srv/klayout_server.py --port "${DRC_PORT}"
