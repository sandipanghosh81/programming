# docker/klayout/Dockerfile  —  KLayout headless DRC + noVNC debug viewer
# ─────────────────────────────────────────────────────────────────────────────
# Provides:
#   - KLayout 0.29 with Python scripting (for DRC and API stub tests)
#   - Xvfb virtual display (required even in headless DRC mode)
#   - x11vnc + noVNC for browser-based layout debugging
#   - A tiny Python HTTP server that receives /run_drc requests

FROM ubuntu:24.04

# Suppress interactive prompts from apt
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    # Core
    python3 python3-pip \
    # KLayout runtime deps
    libqt5widgets5 libqt5xml5 libqt5sql5 \
    libglib2.0-0 \
    # Virtual display + VNC
    xvfb x11vnc novnc websockify \
    # Utilities
    curl wget procps \
    && rm -rf /var/lib/apt/lists/*

# Install KLayout from official .deb (Ubuntu/Debian package)
# Pin to 0.29.x for stability; update KLAYOUT_VERSION to upgrade
ARG KLAYOUT_VERSION=0.29.4
RUN wget -q \
    "https://www.klayout.org/downloads/Ubuntu-24/klayout_${KLAYOUT_VERSION}-1_amd64.deb" \
    -O /tmp/klayout.deb \
    && apt-get install -y /tmp/klayout.deb \
    && rm /tmp/klayout.deb

# Python packages for the HTTP server and DRC runner
RUN pip3 install --no-cache-dir fastapi uvicorn httpx

# Copy service scripts
COPY docker/klayout/klayout_server.py /srv/klayout_server.py
COPY docker/klayout/drc_runner.sh     /usr/local/bin/drc_runner.sh
COPY docker/klayout/start-klayout-vnc.sh /usr/local/bin/start-klayout-vnc.sh
RUN chmod +x /usr/local/bin/drc_runner.sh /usr/local/bin/start-klayout-vnc.sh

# Entrypoint starts Xvfb, noVNC, and the HTTP server
CMD ["/usr/local/bin/start-klayout-vnc.sh"]

EXPOSE 8002 6080
