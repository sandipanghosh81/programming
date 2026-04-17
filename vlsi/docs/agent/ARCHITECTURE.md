# VLSI Agent — Overall System Architecture (v4)
# Cross-referenced with:
#   - docs/proposed_architecture_spec_v3.md (Python LangGraph detail)
#   - ../../cpp_programs/eda_router/docs/architecture_v3.md (C++ engine detail)
#   - ../../cpp_programs/eda_cli/README.md (CLI / MCP gateway detail)

## Architecture at a Glance

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        KLayout EDA Tool                                 │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │  klayout_macro/chatbot_dock.py   (PyQt dock widget)             │   │
│  │  • User types a command (e.g. "route the power grid")            │   │
│  │  • Sends HTTP POST to http://127.0.0.1:8000/chat                 │   │
│  │  • Receives JSON {reply, viewer_commands}                        │   │
│  │  • Executes viewer_commands on the KLayout canvas                │   │
│  └──────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
                                    │  HTTP
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│              server.py  (FastAPI, port 8000)                            │
│  Entry point for all LangGraph invocations.                             │
│  Creates the Orchestrator Graph A and routes every chat request to it.  │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│           agent/orchestrator_graph.py   [Graph A]                       │
│                                                                         │
│  ┌────────────┐    ┌────────────────┐    ┌─────────────────────────┐   │
│  │ parse_intent│    │ dispatch_module│    │  dispatch_workflow       │   │
│  │  (LLM call) │───▶│  (direct tool  │    │  (EDA workflow router)   │   │
│  └────────────┘    │  call to m1-m4) │    └─────────────────────────┘   │
│                    └────────────────┘                │                  │
│                                                      │                  │
│  DIRECT MODULE ACCESS:            WORKFLOW DISPATCH: │                  │
│  A calls m1..m4 directly          A calls w1..w2 ────┘                  │
│  for simple single-tool tasks.    for multi-step EDA pipelines.         │
└─────────────────────────────────────────────────────────────────────────┘
         │                    │                    │
         ▼                    ▼                    ▼
  ┌─────────────┐    ┌──────────────┐    ┌──────────────────────────────┐
  │ m1: Router  │    │ m2: Placer   │    │  w1: Full Placement+Route     │
  │ Subgraph    │    │ Subgraph     │    │  w2: DRC Analysis + Fix Loop  │
  │             │    │              │    │  (these call m1..m4 via       │
  │ m3: DB      │    │ m4: Window   │    │   shared CLI — no direct      │
  │ Subgraph    │    │ Automation   │    │   Python-to-Python coupling)  │
  └──────┬──────┘    └──────┬───────┘    └──────────────────────────────┘
         │                  │
         └──────────────────┘
                    │
         All modules communicate ONLY through:
                    │
                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│            eda_cli  (C++ CLI + WebSocket MCP Gateway, port 8080)        │
│                                                                         │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │ JSON-RPC dispatch over ws://127.0.0.1:8080                        │  │
│  │                                                                   │  │
│  │  method: "load_design"       → DB MCP server                      │  │
│  │  method: "route_nets"        → Router MCP server                  │  │
│  │  method: "place_cells"       → Placer MCP server                  │  │
│  │  method: "view.zoom_to"      → Window MCP server                  │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                                                         │
│  SHARED MEMORY (SharedDatabase):                                        │
│    Owned by eda_cli process.                                            │
│    All MCP servers (Router, Placer, DB, Window) are compiled INTO       │
│    eda_cli and share a single SharedDatabase* — zero IPC overhead.      │
└─────────────────────────────────────────────────────────────────────────┘
         │               │                │
         ▼               ▼                ▼
  routing_genetic_astar  eda_placer  eda_db_reader
  (C++ library)          (future)    (future)
```

## Design Rules (anti-coupling guarantees)

1. **No Python module calls another Python module directly.**
   m1 never imports anything from m2.  All cross-module communication goes
   through Graph A or through the C++ CLI JSON-RPC interface.

2. **No C++ module calls another C++ module through Python.**
   All C++ modules (Router, Placer, DB) share one process memory space via
   SharedDatabase.  They communicate through direct C++ function calls INSIDE
   the eda_cli binary, never by going back up through Python.

3. **The C++ CLI (eda_cli) is independent of any algorithm module.**
   It is the gateway, not the algorithm.  Algorithm modules (routing_genetic_astar,
   eda_placer) are compiled as libraries and linked into eda_cli.

4. **Workflows (w1, w2) own their multi-step iteration logic.**
   Workflows use module subgraphs but do not expose their internals to Graph A.
   Graph A only sees: "start workflow w1" and "workflow w1 finished".

## File Map

```
vlsi/agent/
  server.py                         FastAPI entry point (port 8000)
  main.py                           CLI entry point (dev/test)
  agent/
    orchestrator_graph.py           Graph A — master orchestrator
    modules/
      m1_router_subgraph.py         Router module subgraph
      m2_placer_subgraph.py         Placer module subgraph
      m3_db_subgraph.py             DB reader module subgraph
      m4_window_subgraph.py         Window automation module subgraph
      _cli_client.py                Shared WebSocket MCP client (all modules use this)
    workflows/
      w1_full_route_flow.py         Full placement + routing workflow
      w2_drc_fix_loop.py            Iterative DRC analysis + correction workflow
  klayout_macro/
    chatbot_dock.py                 KLayout PyQt dock widget (chatbot UI)

vlsi/cpp/
  eda_cli/                          NEW: standalone C++ CLI + MCP gateway
    src/
      main.cpp                      CLI entry point (start daemon, parse args)
      eda_daemon.cpp                WebSocket MCP dispatcher
    include/
      eda_cli/shared_database.hpp   Shared memory object (all servers share this)
    CMakeLists.txt
  routing_genetic_astar/            Router algorithm library (no CLI bundled)
    include/ src/ tests/            ... (unchanged internal structure)
```

## Starting the System (step by step)

```
1. Terminal A:  cd eda_cli && ./build/eda_daemon
   → C++ daemon starts, listens on ws://127.0.0.1:8080
   → All MCP servers (Router, Placer, DB, Window) ready

2. Terminal B:  cd vlsi/agent && python server.py
   → FastAPI starts, listens on http://127.0.0.1:8000
   → LangGraph Orchestrator Graph A compiled and ready

3. KLayout:     Run klayout_macro/chatbot_dock.py from Macro Editor
   → ChatbotDock dock panel appears in KLayout
   → User types "route the power rail VDD"
   → Request flows: KLayout → server.py → Graph A → m1 subgraph → eda_cli → router library
   → Result flows back: router library → eda_cli → m1 → Graph A → server.py → KLayout
   → KLayout receives viewer_commands and refreshes the canvas
```
