## VLSI Agent — File Map & Execution Order

This document explains:

- **What each important file does**
- **The order functions are invoked** from KLayout → Python → C++ → back to KLayout

### High-level invocation flow (runtime order)

1. **KLayout UI**
   - File: `vlsi/agent/klayout_macro/chatbot_dock.py`
   - Entry: `ChatbotDock.send_message()`
   - Sends HTTP POST to the agent server:
     - `POST http://127.0.0.1:8000/chat` with `{"message": "<user text>"}`

2. **Python HTTP server**
   - File: `vlsi/agent/server.py`
   - Startup:
     - `create_orchestrator_graph()` is called once at import/startup to compile Graph A
   - Per request:
     - `chat_endpoint()` receives the request, builds initial state, calls:
       - `await master_graph.ainvoke(initial_state)`

3. **Graph A (master orchestrator)**
   - File: `vlsi/agent/src/orchestrator/orchestrator_graph.py`
   - Graph topology:
     - `parse_intent(state)` (LLM) → sets `active_intent` and `intent_params`
     - `dispatch_intent(state)` routes to one of:
       - `m1_router` (route)
       - `m2_placer` (place)
       - `m3_db` (db_query)
       - `m4_window` (view)
       - `w1_full_route` / `w2_drc_fix` workflows
       - `chat_node` (general)
     - all paths then go to:
       - `collect_outputs(state)` → appends `{"action":"unlock_ui"}` when needed

4. **Module subgraphs (m1..m4)**

#### m2 — Placer module (place)
- File: `vlsi/agent/src/orchestrator/modules/m2_placer_subgraph.py`
- Node order:
  1. `validate_placement_input()` → checks daemon reachability
  2. `call_placer_via_cli()` → calls placement via MCP
     - For SPICE-driven analog placement:
       - If `placement_params.spice_netlist_path` is provided, it builds an `analog_problem`
  3. `format_placer_response()` → returns user-facing message + viewer commands:
     - `draw_instances` on layer 999/0
     - `draw_routes` on layer 998/0 (early route visualization)

#### m1 — Router module (route)
- File: `vlsi/agent/src/orchestrator/modules/m1_router_subgraph.py`
- Node order:
  1. `validate_routing_input()`
  2. `call_router_via_cli()`
  3. `format_router_response()`

#### m3 — DB module (db_query)
- File: `vlsi/agent/src/orchestrator/modules/m3_db_subgraph.py`
- Calls DB-related MCP methods (status, net lists, bboxes, etc.)

#### m4 — Window module (view)
- File: `vlsi/agent/src/orchestrator/modules/m4_window_subgraph.py`
- Produces viewer commands like `zoom_to`, `refresh_view`, etc.

5. **MCP client used by modules**
- File: `vlsi/agent/src/orchestrator/modules/_cli_client.py`
- Key function:
  - `mcp_call(method, params)` → JSON-RPC over WebSocket to C++ daemon at `ws://127.0.0.1:8080`

6. **C++ daemon (WebSocket JSON-RPC gateway)**
- Built from: `vlsi/eda_tools/eda_cli/`
- Binary: `vlsi/eda_tools/eda_cli/build/eda_daemon`
- Dispatch implementation:
  - File: `vlsi/eda_tools/eda_cli/src/eda_daemon.cpp`
  - Receives JSON-RPC, routes methods like:
    - `load_design`, `db.status`, `route_nets`, `place_cells`, ...

7. **Constraints tool (Python MCP server, optional but preferred for SPICE)**
- Server:
  - File: `vlsi/eda_tools/python/constraints_tool/mcp_server.py`
  - Methods:
    - `constraints.extract` → parses SPICE and returns distilled design constraints
- Client:
  - File: `vlsi/agent/src/orchestrator/utils/constraints_mcp_client.py`

8. **Back to KLayout (viewer commands executed)**
- File: `vlsi/agent/klayout_macro/chatbot_dock.py`
- Entry: `ChatbotDock._process_viewer_commands(commands)`
- Relevant actions implemented:
  - `draw_instances` → draws rectangles (layer 999/0)
  - `draw_routes` → draws early route segments (layer 998/0)
  - `zoom_fit`, `refresh_view`, `screenshot`, ...

---

### Key utility files (what they do)

- **Early routing visualizer**
  - File: `vlsi/agent/src/orchestrator/utils/early_router.py`
  - Function: `build_early_routes(...)`
  - Produces Manhattan “preview routing” segments for KLayout visualization.

- **SPICE → analog placement problem**
  - File: `vlsi/agent/src/orchestrator/utils/spice_to_analog_problem.py`
  - Function: `build_analog_problem_from_spice(...)`
  - Builds a tool-neutral `analog_problem` for the C++ placer, preferably via the constraints MCP tool.

