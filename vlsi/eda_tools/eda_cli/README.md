# eda_cli — Standalone EDA Command-Line Gateway
# Cross-reference: ../vlsi_agent_python/ARCHITECTURE.md

## What This Project Is

`eda_cli` is the **C++ gateway process** that:
1. Hosts a WebSocket server (port 8080) accepting JSON-RPC 2.0 requests
2. Maintains a **SharedDatabase** in memory (single source of truth for all EDA state)
3. Dispatches requests to algorithm libraries (e.g., `routing_genetic_astar`)
4. Returns results as JSON back to the Python LangGraph agent

## Why Does eda_cli Exist As a Separate Project?

Previously the WebSocket daemon was **bundled inside `routing_genetic_astar`**,
meaning the routing algorithm owned the network infrastructure.  This was wrong:
- Adding a placer required modifying the routing project's CMakeLists
- The transport layer changed every time an algorithm changed
- Testing the router required a live network connection

`eda_cli` separation means:
- `routing_genetic_astar` is a pure algorithm library — no network code
- `eda_cli` is the gateway — no algorithm code  
- New algorithms (eda_placer, eda_drc) link into eda_cli as libraries

## Directory Structure

```
eda_cli/
  src/
    main.cpp           CLI entry point: parse --port, start daemon
    eda_daemon.cpp     WebSocket server + JSON-RPC dispatch
    mcp/
      db_server.cpp    DB MCP server (load_design, db.status, db.query_nets, ...)
      router_server.cpp Router MCP server (route_nets, eco.fix_drc, drc.check)
      placer_server.cpp Placer MCP server (place_cells)
      window_server.cpp Window MCP server (window.current_view, db.net_bbox)
  include/
    eda_cli/
      shared_database.hpp  SharedDatabase struct (shared across all MCP servers)
      eda_daemon.hpp       Daemon class declaration
  CMakeLists.txt
```

## JSON-RPC Method Table

| Method              | Handler                  | Description                          |
|---------------------|--------------------------|--------------------------------------|
| `ping`              | eda_daemon               | Health check → "pong"                |
| `load_design`       | DbMcpServer              | Parse DEF/LEF, build routing lattice |
| `db.status`         | DbMcpServer              | is_loaded, design_name, net_count    |
| `db.query_nets`     | DbMcpServer              | List all net IDs and names           |
| `db.query_cells`    | DbMcpServer              | List cell instances and positions    |
| `db.query_bbox`     | DbMcpServer              | Design bounding box                  |
| `db.net_bbox`       | DbMcpServer              | Bounding box of a specific net       |
| `route_nets`        | RouterMcpServer          | Run GA + PathFinder routing engine   |
| `drc.check`         | RouterMcpServer          | Enumerate DRC violations             |
| `eco.fix_drc`       | RouterMcpServer          | ECO re-route violating nets          |
| `place_cells`       | PlacerMcpServer          | Run cell placement                   |
| `window.current_view`| WindowMcpServer         | Get current KLayout view bounds      |

## How to Build

```bash
cd eda_cli
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/eda_daemon --port 8080
```

## How Algorithm Libraries Link In

```cmake
# eda_cli/CMakeLists.txt
find_package(routing_genetic_astar REQUIRED)    # Algorithm library
target_link_libraries(eda_daemon PRIVATE routing_genetic_astar::router)
```

The routing library exposes a simple C++ API:
  `RouterMcpServer::route_nets(params) → nlohmann::json`
The eda_daemon calls this and forwards the JSON result over WebSocket.
