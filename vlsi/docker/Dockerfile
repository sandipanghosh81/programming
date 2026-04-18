# Dockerfile  —  VLSI Agent multi-stage build
# ─────────────────────────────────────────────────────────────────────────────
# Stage 1 (builder): compile the C++ EDA tools (eda_router, eda_placer, eda_cli)
# Stage 2 (agent):   Python agent with all dependencies
#
# The resulting image runs the Python agent server (FastAPI/uvicorn).
# The C++ daemon (eda_daemon) is also baked in so docker-compose can start it.
#
# BUILD:
#   docker build -t vlsi-agent:latest -f docker/Dockerfile .
#
# RUN (agent only — daemon managed by compose):
#   docker run --rm -p 8000:8000 vlsi-agent:latest

# ── Stage 1: C++ build ────────────────────────────────────────────────────────
FROM ubuntu:24.04 AS cpp-builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake ninja-build \
    g++ \
    libboost-all-dev \
    nlohmann-json3-dev \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Copy only eda_tools source (avoids rebuilding on Python changes)
COPY eda_tools/ ./eda_tools/

# Build eda_router (routing_genetic_astar_io + vlsi_daemon)
RUN cmake -S eda_tools/eda_router -B /build/eda_router \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_STANDARD=23 \
    && cmake --build /build/eda_router --target routing_genetic_astar_io vlsi_daemon -j$(nproc)

# Build eda_cli (eda_daemon)
RUN cmake -S eda_tools/eda_cli -B /build/eda_cli \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_STANDARD=23 \
    && cmake --build /build/eda_cli --target eda_daemon -j$(nproc)

# ── Stage 2: Python agent ─────────────────────────────────────────────────────
FROM python:3.12-slim AS agent

LABEL org.opencontainers.image.title="vlsi-agent"
LABEL org.opencontainers.image.description="LangGraph VLSI EDA orchestration agent"

# Runtime deps for C++ binaries
RUN apt-get update && apt-get install -y --no-install-recommends \
    libboost-system1.83.0 \
    && rm -rf /var/lib/apt/lists/*

# Copy compiled C++ binaries from builder stage
COPY --from=cpp-builder /build/eda_cli/eda_daemon  /usr/local/bin/eda_daemon
COPY --from=cpp-builder /build/eda_router/vlsi_daemon /usr/local/bin/vlsi_daemon

WORKDIR /app

# Install Python dependencies (layer-cached; only rebuilds if pyproject.toml changes)
COPY agent/pyproject.toml ./
RUN pip install --no-cache-dir -e ".[rag,drc]" || \
    pip install --no-cache-dir \
        fastapi>=0.115.0 uvicorn>=0.29.0 \
        langgraph>=0.2.0 langchain>=0.3.0 langchain-core>=0.3.0 \
        langchain-google-genai>=2.0.0 \
        python-dotenv>=1.0.0 \
        websockets>=12.0 pydantic>=2.6.0 httpx>=0.27.0 \
        jinja2>=3.1.0 \
        chromadb>=0.5.0 sentence-transformers>=3.0.0

# Copy agent source
COPY agent/ ./

# Non-root user for security
RUN useradd -m -u 1001 agentuser
USER agentuser

EXPOSE 8000

ENV EDA_CLI_HOST=eda-daemon \
    EDA_CLI_PORT=8080 \
    CHROMADB_HOST=chromadb \
    CHROMADB_PORT=8001 \
    KLAYOUT_HOST=klayout \
    KLAYOUT_PORT=8002 \
    EDA_SHARE=/eda_share

CMD ["uvicorn", "server:app", "--host", "0.0.0.0", "--port", "8000", "--workers", "1"]
