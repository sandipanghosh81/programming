# VLSI Chatbot (KLayout) — How to Run

This guide explains how to run the full stack:

- **KLayout macro UI** (`klayout_macro/chatbot_dock.py`)
- **Python agent server** (`server.py`, HTTP on `127.0.0.1:8000`)
- **C++ daemon** (`eda_daemon`, WebSocket JSON-RPC on `127.0.0.1:8080`)

It also covers the **analog placement + early routing visualization** path:

- **SPICE netlist** → `vlsi/agent` → `place_cells` → KLayout draws:
  - placed instances on **layer 999/0**
  - early routes on **layer 998/0**

---

## Prerequisites

- **KLayout** installed (with Python scripting enabled)
- **Python 3** available (your repo includes a `.venv` for `vlsi/agent`, if set up)
- **CMake + C++ toolchain** for building the C++ daemon

Ports used:

- **8000**: Python agent server (HTTP)
- **8080**: C++ daemon (WebSocket JSON-RPC)

---

## 1) Build the C++ daemon

From the repo root (`programming/`):

```bash
cmake -S "vlsi/eda_tools/eda_cli" -B "vlsi/eda_tools/eda_cli/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "vlsi/eda_tools/eda_cli/build" -j
```

Or using the convenience Makefile:

```bash
cd "/Users/sandipanghosh/programming/vlsi"
make build        # alias for build-daemon
```

Binary output:

- `vlsi/eda_tools/eda_cli/build/eda_daemon`

---

## 2) Start the C++ daemon (WebSocket on :8080)

Recommended: start the full stack (daemon + Python services) using the Makefile:

```bash
cd "/Users/sandipanghosh/programming/vlsi"
make daemon
```

This starts:

- constraints MCP server (WebSocket, `:18081`)
- Python agent server (HTTP, `:8000`)
- C++ daemon (WebSocket, `:8080`)

Alternative (daemon only, without auto-starting Python services):

```bash
./vlsi/eda_tools/eda_cli/build/eda_daemon
```

Optional (custom port, daemon only):

```bash
./vlsi/eda_tools/eda_cli/build/eda_daemon --port 8080
```

You should see a line like:

- `Waiting for connections on ws://127.0.0.1:8080`

---

## 3) Start the Python agent server (HTTP on :8000)

If you started the stack with `make daemon` above, you do **not** need to run this separately.

Health check:

```bash
curl http://127.0.0.1:8000/health
```

Expected:

```json
{"status":"ok","agent":"ready"}
```

---

## 4) Install & run the KLayout macro

In KLayout:

1. **Macros → Macro Development**
2. **File → Import** and select:
   - `vlsi/agent/klayout_macro/chatbot_dock.py`
3. Run the macro (Run button / `F5`)

You should see a **“VLSI Agent”** dock widget.

---

## 5) Try a basic chat command

In the dock, type:

- `hello`

You should get an agent response and the UI should unlock after the request.

---

## 6) Analog placement + early routing visualization (SPICE)

Your SPICE netlists live here:

- `vlsi/data/eda_data/netlists/`

Example netlist:

- `vlsi/data/eda_data/netlists/opamp_netlist.sp`

In the chatbot, try:

- `place /Users/sandipanghosh/programming/vlsi/data/eda_data/netlists/opamp_netlist.sp`

What you should see in KLayout:

- **Layer 999/0**: rectangles for placed devices
- **Layer 998/0**: early Manhattan route segments (visualization only)

If you don’t see anything:

- Ensure a layout is open / a cellview exists in KLayout
- Ensure both servers are running (Steps 2 and 3)
- In the agent logs, confirm `place_cells` returned `placed`

---

## Troubleshooting

### “Agent server NOT reachable”

- Confirm `python server.py` is running
- Confirm port `8000` is free
- Try `curl http://127.0.0.1:8000/health`

### “Cannot connect to eda_cli daemon”

- Confirm `./vlsi/eda_tools/eda_cli/build/eda_daemon` is running
- Confirm port `8080` is free

### Nothing draws in KLayout

The macro draws into the **current active cellview**. Make sure:

- you have an open layout
- there’s an active top cell selected

---

## Notes

- The early routing drawn is **not DRC-clean** and does not use the PDK metal stack yet.
- It’s meant to be a fast visual feedback loop for analog placement + connectivity.

