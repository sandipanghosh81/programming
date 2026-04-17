# VLSI Agent — Architecture

> Single source of truth for the VLSI agent's architecture. For the C++ routing-engine internals, see [`implementation.md`](./implementation.md). For how to run the stack end-to-end, see [`chatbot_howto.md`](./chatbot_howto.md).

## Table of contents

1. [At a Glance](#1-at-a-glance)
2. [Components](#2-components)
3. [Request Flow](#3-request-flow)
4. [Modules and Workflows](#4-modules-and-workflows)
5. [Interfaces and Contracts](#5-interfaces-and-contracts)
6. [Design Rules](#6-design-rules)
7. [Running the Stack](#7-running-the-stack)
8. [Future Direction (v3)](#8-future-direction-v3)
9. [Appendices](#9-appendices)

---

## 1. At a Glance

A user types a command into a KLayout dock panel. That request is routed through a Python LangGraph orchestrator which decides whether to answer directly or invoke an EDA module. Modules and workflows talk to a single C++ daemon over WebSocket JSON-RPC; the daemon hosts all algorithm code (router, placer, DB, window) in one process with a shared database.

```mermaid
flowchart LR
    User([User])
    KL["KLayout dock<br/>chatbot_dock.py"]
    API["FastAPI server<br/>:8000 /chat"]
    G["LangGraph<br/>Orchestrator (Graph A)"]
    D["eda_daemon<br/>:8080 WebSocket"]

    User --> KL
    KL -- HTTP POST --> API
    API --> G
    G -- JSON-RPC --> D
    D --> G
    G --> API
    API --> KL
    KL --> User

    style G fill:#e67e22,color:#fff
    style D fill:#2ecc71,color:#fff
    style KL fill:#4a90d9,color:#fff
```

**Three processes, one shared language (JSON).** The KLayout macro, the agent server, and the C++ daemon each run independently and can be restarted without bringing the others down.

---

## 2. Components

Four cooperating pieces. Read this table first; drill into later sections only for the piece you're changing.

| Component | Process / Port | File(s) | Responsibility |
|---|---|---|---|
| **KLayout Chatbot** | KLayout (PyQt dock) | `vlsi/agent/klayout_macro/chatbot_dock.py` | Capture user text, show replies, render `viewer_commands` on the canvas |
| **Agent Server** | Python / FastAPI · `:8000` | `vlsi/agent/server.py` | HTTP entry point; owns one compiled LangGraph instance |
| **Orchestrator (Graph A)** | Inside agent server | `vlsi/agent/src/orchestrator/orchestrator_graph.py` | LLM intent parsing, dispatch to modules or workflows |
| **C++ Daemon** | Native binary · `:8080` (WS) | `vlsi/eda_tools/eda_cli/` | JSON-RPC gateway; router / placer / DB / window all linked in one process sharing a `SharedDatabase` |

Two auxiliary pieces extend the flow without adding processes:

- **Constraints MCP tool** (`:18081`) — Python MCP server parsing SPICE into a tool-neutral `analog_problem`; used by the placer path.
- **Module subgraphs and workflows** — internal LangGraph nodes (`m1`…`m4`, `w1`, `w2`) that compose simple and multi-step EDA tasks.

```mermaid
graph TB
    subgraph KLayout["KLayout process"]
        dock["chatbot_dock.py<br/>PyQt dock widget"]
    end

    subgraph AgentSvc["Agent server (Python)"]
        api[":8000 FastAPI<br/>server.py"]
        subgraph GraphA["LangGraph Orchestrator — Graph A"]
            intent["parse_intent"]
            dispatch{"dispatch"}
            modules["m1 router<br/>m2 placer<br/>m3 db<br/>m4 window"]
            workflows["w1 full route<br/>w2 drc fix loop"]
            chat["chat_node"]
            outputs["collect_outputs"]
        end
    end

    subgraph CppDaemon["C++ daemon process"]
        dispatcher[":8080 eda_daemon.cpp<br/>JSON-RPC dispatcher"]
        shared[("SharedDatabase<br/>in-process")]
        router["Router library"]
        placer["Placer library"]
        dbserver["DB reader"]
        winauto["Window automation"]
        dispatcher --> router & placer & dbserver & winauto
        router & placer & dbserver & winauto --- shared
    end

    subgraph Constraints["Constraints MCP (optional)"]
        cmcp[":18081 MCP server<br/>constraints.extract"]
    end

    dock -- HTTP /chat --> api
    api --> intent --> dispatch
    dispatch --> modules
    dispatch --> workflows
    dispatch --> chat
    outputs --> api

    modules -- ws JSON-RPC --> dispatcher
    workflows -- ws JSON-RPC --> dispatcher
    modules -. SPICE .-> cmcp

    api -- viewer_commands --> dock

    style dock fill:#4a90d9,color:#fff
    style GraphA fill:#2a1a3e,color:#fff
    style dispatcher fill:#2ecc71,color:#fff
    style shared fill:#27ae60,color:#fff
    style cmcp fill:#9b59b6,color:#fff
```

---

## 3. Request Flow

A single chat turn, end-to-end. Later sections expand each hop.

```mermaid
sequenceDiagram
    autonumber
    participant U as User
    participant K as KLayout dock
    participant S as FastAPI /chat
    participant G as Graph A
    participant M as Module / Workflow
    participant D as eda_daemon

    U->>K: Types command
    K->>S: POST {"message": "..."}
    S->>G: master_graph.ainvoke(state)
    G->>G: parse_intent (LLM)
    G->>M: dispatch_intent → m1/m2/m3/m4 or w1/w2
    M->>D: JSON-RPC (load_design, place_cells, ...)
    D-->>M: result payload
    M-->>G: {reply, viewer_commands}
    G->>G: collect_outputs (unlock_ui)
    G-->>S: final state
    S-->>K: JSON response
    K->>K: _process_viewer_commands(...)
    K-->>U: Reply + canvas update
```

**Reading the flow.** The only LLM call in the hot path is `parse_intent` (step 4). Everything else is deterministic routing and tool invocation. This keeps latency predictable and debugging tractable.

---

## 4. Modules and Workflows

Two kinds of children under Graph A:

- **Modules** (`m1…m4`) — single-tool wrappers. They validate input, make one call to the daemon, format a response. Use them for "do one thing" intents.
- **Workflows** (`w1`, `w2`) — multi-step pipelines that may call several modules internally and iterate. Use them when the user asks for a compound outcome (full P&R, DRC fix loop).

```mermaid
graph TD
    A["Graph A<br/>dispatch_intent"] -->|route| M1["m1 router"]
    A -->|place| M2["m2 placer"]
    A -->|db_query| M3["m3 db"]
    A -->|view| M4["m4 window"]
    A -->|full flow| W1["w1 full_route_flow"]
    A -->|drc fix| W2["w2 drc_fix_loop"]
    A -->|general| C["chat_node"]

    W1 -. uses .-> M2
    W1 -. uses .-> M1
    W1 -. uses .-> M4
    W2 -. uses .-> M3
    W2 -. uses .-> M1

    style A fill:#e67e22,color:#fff
    style W1 fill:#f39c12,color:#fff
    style W2 fill:#f39c12,color:#fff
```

<details>
<summary><b>m2 — Placer subgraph (detail)</b></summary>

File: `vlsi/agent/src/orchestrator/modules/m2_placer_subgraph.py`

Nodes, in order:

1. `validate_placement_input` — checks daemon reachability.
2. `call_placer_via_cli` — invokes placement via MCP. If `placement_params.spice_netlist_path` is set, builds an `analog_problem` via the **constraints MCP tool** (preferred) or the local fallback in `utils/spice_to_analog_problem.py`.
3. `format_placer_response` — returns the user-facing message plus viewer commands:
   - `draw_instances` on layer `999/0`
   - `draw_routes` on layer `998/0` (early-route visualization, built by `utils/early_router.build_early_routes`)

</details>

<details>
<summary><b>m1 — Router subgraph (detail)</b></summary>

File: `vlsi/agent/src/orchestrator/modules/m1_router_subgraph.py`

Nodes: `validate_routing_input` → `call_router_via_cli` → `format_router_response`.

Uses the shared `_cli_client.mcp_call(method, params)` over WebSocket to the daemon (`ws://127.0.0.1:8080`).

</details>

<details>
<summary><b>m3 / m4 — DB and Window subgraphs</b></summary>

- `m3_db_subgraph.py` — status queries, net lists, bounding boxes. Calls DB-side MCP methods.
- `m4_window_subgraph.py` — emits viewer commands (`zoom_to`, `refresh_view`, `screenshot`, ...). These are serialized back to the KLayout dock which executes them on the canvas.

</details>

<details>
<summary><b>w1 / w2 — Workflows</b></summary>

- `w1_full_route_flow.py` — orchestrates placer → router → window refresh for a full P&R turn.
- `w2_drc_fix_loop.py` — iterative DRC correction. Queries violations via `m3`, re-routes offenders via `m1`, repeats until clean or retry budget is exhausted.

Workflows expose only `start` / `finished` edges to Graph A — Graph A never sees their internal iteration.

</details>

---

## 5. Interfaces and Contracts

Three boundaries — each one is a narrow, auditable contract.

### 5.1 Dock ↔ Agent server (HTTP)

```
POST http://127.0.0.1:8000/chat
Content-Type: application/json

Request:  {"message": "<user text>"}
Response: {"reply": "<text>", "viewer_commands": [ {...}, ... ]}
```

`viewer_commands` is the only way the agent changes the canvas. The dock's `_process_viewer_commands(commands)` implements actions such as `draw_instances`, `draw_routes`, `zoom_fit`, `refresh_view`, `screenshot`.

### 5.2 Agent ↔ C++ daemon (WebSocket JSON-RPC)

All module and workflow code calls `mcp_call(method, params)` in `modules/_cli_client.py`. Representative methods:

| Method | Owner inside daemon |
|---|---|
| `load_design` | DB server |
| `db.status`, `db.get_nets`, `db.get_bboxes` | DB server |
| `route_nets` | Router server |
| `place_cells` | Placer server |
| `view.zoom_to`, `view.refresh` | Window server |

### 5.3 Agent ↔ Constraints MCP (optional, SPICE path)

- **Server** `vlsi/eda_tools/python/constraints_tool/mcp_server.py` exposes `constraints.extract`.
- **Client** `vlsi/agent/src/orchestrator/utils/constraints_mcp_client.py` is called by `m2` when a SPICE netlist is provided.

---

## 6. Design Rules

Four anti-coupling guarantees the code base enforces:

1. **No Python module calls another Python module directly.** `m1` never imports from `m2`. Cross-module communication goes through Graph A or through the C++ daemon's JSON-RPC interface.
2. **No C++ module calls another C++ module through Python.** Router/Placer/DB all share one process memory via `SharedDatabase`. Inter-module calls are direct C++ function calls inside the daemon binary — never a round-trip through Python.
3. **`eda_cli` is the gateway, not an algorithm.** Algorithm modules (e.g. `routing_genetic_astar`, `eda_placer`) are compiled as libraries and linked into the daemon.
4. **Workflows own their iteration logic.** `w1`, `w2` use module subgraphs internally but expose only `start` / `finished` to Graph A.

---

## 7. Running the Stack

Three terminals, three ports. See [`chatbot_howto.md`](./chatbot_howto.md) for the full guide including troubleshooting.

```mermaid
sequenceDiagram
    participant T1 as Terminal A
    participant T2 as Terminal B
    participant T3 as KLayout
    Note over T1: cd vlsi && make daemon
    T1->>T1: Starts eda_daemon (:8080)<br/>+ constraints MCP (:18081)<br/>+ server.py (:8000)
    Note over T2: Optional health check
    T2->>T1: GET http://127.0.0.1:8000/health
    T1-->>T2: {"status":"ok","agent":"ready"}
    Note over T3: Import & run macro<br/>klayout_macro/chatbot_dock.py
    T3->>T1: POST /chat {"message":"hello"}
    T1-->>T3: {"reply":"...","viewer_commands":[]}
```

| Port | Process | Purpose |
|---|---|---|
| `:8000` | Python `server.py` | HTTP `/chat` |
| `:8080` | C++ `eda_daemon` | WebSocket JSON-RPC |
| `:18081` | Python `constraints_tool` MCP | SPICE parsing (optional) |

---

## 8. Future Direction (v3)

This section describes the **target architecture** — a distributed mini-orchestrator model where each MCP server has its own LangGraph sub-graph and can query peers before invoking the underlying C++ engine. The current system (above) is a stepping-stone toward this.

### 8.1 Model in One Picture

```mermaid
graph TB
    subgraph PY["Python LangGraph Process"]
        MG["Master Graph<br/>Router · Planner · Verifier"]
        MP["Placement Sub-Graph"]
        MR["Routing Sub-Graph"]
        ME["Editing Sub-Graph"]
        MG -->|delegate| MP
        MG -->|delegate| MR
        MG -->|delegate| ME
    end
    subgraph CPP["C++ EDA Process (execution only)"]
        DB["DB MCP (always-on)"]
        WIN["Windows MCP (always-on)"]
        P["Placement engine (on-demand)"]
        R["Routing engine (on-demand)"]
        E["Editing engine (on-demand)"]
    end
    subgraph LLMG["LLM"]
        LLM["Commercial or local"]
    end
    MP <-->|MCP| DB
    MP <-->|MCP| WIN
    MP <-->|MCP| P
    MR <-->|MCP| DB
    MR <-->|MCP| R
    ME <-->|MCP| DB
    ME <-->|MCP| E
    MG <-->|REST| LLM
    MP <-->|REST| LLM
    MR <-->|REST| LLM

    style MG fill:#e67e22,color:#fff
    style MP fill:#f39c12,color:#fff
    style MR fill:#f39c12,color:#fff
    style ME fill:#f39c12,color:#fff
    style DB fill:#2ecc71,color:#fff
    style WIN fill:#2ecc71,color:#fff
    style LLM fill:#9b59b6,color:#fff
```

### 8.2 Principles

1. **Master stays lightweight** — it routes, plans, and verifies. Domain logic lives in sub-graphs.
2. **C++ is execution-only** — servers never call LLMs or make routing decisions.
3. **Inter-server queries are fast** — same-process servers use direct memory; out-of-process servers use MCP. One unified `ToolClient` interface hides the transport.
4. **Sub-graphs are autonomous** — each domain sub-graph queries DB/Windows/RAG independently to resolve its own parameters.
5. **Modularity preserved** — C++ owns capability exposure; Python owns orchestration.

### 8.3 Master Graph (complete)

```mermaid
graph TD
    S(("Start")) --> R["Router"]
    R -->|design query| QE["Query Exec<br/>DB + RAG"]
    R -->|simple action| SE["Simple Exec"]
    R -->|complex| PL["Planner"]
    R -->|ambiguous| HC["Clarify<br/>(interrupt)"]
    HC --> R
    PL --> PA["Plan Approval<br/>(interrupt)"]
    PA -->|approved| LC["Tool Lifecycle<br/>start on-demand servers"]
    PA -->|modify| PL
    PA -->|cancel| E((End))
    LC --> MX["Master Executor"]
    MX -->|P&R| PSG["Placement SG"]
    MX -->|Routing| RSG["Routing SG"]
    MX -->|Edit| ESG["Editing SG"]
    SE -->|MCP| VMCP["Viewing MCP"]
    SE -->|MCP| EMCP["Editing MCP"]
    PSG & RSG & ESG & QE & SE --> MV["Master Verifier"]
    MV -->|passed, more| MX
    MV -->|all done| MEM["Memory"]
    MV -->|failed| RP["Re-Planner"]
    RP --> MX
    RP -->|max retries| HE["Escalate"]
    HE --> RP
    MEM --> E

    style R fill:#4a90d9,color:#fff
    style PL fill:#7b68ee,color:#fff
    style MX fill:#e67e22,color:#fff
    style MV fill:#27ae60,color:#fff
    style RP fill:#f39c12,color:#fff
    style HC fill:#e74c3c,color:#fff
    style PA fill:#e74c3c,color:#fff
    style HE fill:#e74c3c,color:#fff
```

### 8.4 Placement Sub-Graph Example

A single delegation with sibling queries, LLM-driven command synthesis, DRC loop, and convergence.

<details>
<summary><b>Expand: "place decoupling caps near power pins" — step-by-step</b></summary>

```mermaid
sequenceDiagram
    participant M as Master
    participant PS as Placement SG
    participant L as LLM
    participant P as Placement MCP
    participant DB as DB MCP
    participant W as Windows MCP

    M->>PS: Place decap caps near power pins
    Note over PS: Self-Diagnosis
    PS->>L: what data do I need?
    L-->>PS: cell names, pin locs, region, viewport
    Note over PS: Sibling Queries
    PS->>DB: get_object_ids(filter="decap_cell")
    DB-->>PS: [id_101, id_102, id_103]
    PS->>DB: get_bounding_box(power_domain_A)
    DB-->>PS: {0,0,500,300}
    PS->>W: get_active_window
    W-->>PS: {layout_1, zoom:0.5}
    Note over PS: Synthesis + Execute
    PS->>L: generate placement params
    L-->>PS: {cells, coords, constraints}
    PS->>P: place_cells(...)
    P-->>PS: {done, violations:3}
    Note over PS: Verifier loop
    PS->>P: run_drc(power_domain_A)
    P-->>PS: 3 overlaps
    PS->>L: adjust placement
    L-->>PS: adjusted_coords
    PS->>P: place_cells(...)
    P-->>PS: {done, violations:0}
    PS->>P: run_drc
    P-->>PS: clean
    PS-->>M: {success, cells:3, iterations:2}
```

</details>

### 8.5 Security & Deployment

- **Dual-model support** — sanitized planning → commercial cloud LLM; data-sensitive specialist loops → on-prem GPU (NIM, vLLM, Ollama).
- **Fully air-gappable** — supports 100% on-premises deployment within the corporate LAN.

---

## 9. Appendices

### 9.1 File Map (current code)

```text
vlsi/agent/
  server.py                            FastAPI entry point (:8000)
  main.py                              CLI entry point (dev/test)
  constraints.py                       constraint helpers (shared)
  pyproject.toml
  klayout_macro/
    chatbot_dock.py                    KLayout PyQt dock widget
    viewer_client.py                   viewer command helpers
  src/orchestrator/
    orchestrator_graph.py              Graph A master orchestrator
    router_subgraph.py                 top-level routing subgraph
    modules/
      _cli_client.py                   shared WebSocket MCP client
      m1_router_subgraph.py            Router module
      m2_placer_subgraph.py            Placer module (analog + SPICE)
      m3_db_subgraph.py                DB reader module
      m4_window_subgraph.py            Window automation module
    workflows/
      w1_full_route_flow.py            Full placement + routing
      w2_drc_fix_loop.py               Iterative DRC correction
    utils/
      constraints_mcp_client.py        constraints.extract client
      early_router.py                  Manhattan early-route preview
      env_bootstrap.py                 environment setup
      spice_to_analog_problem.py       SPICE → analog_problem

vlsi/eda_tools/
  eda_cli/                             C++ CLI + MCP gateway (daemon)
  routing_genetic_astar/               router library
  eda_placer/                          placer library (WIP)
  python/constraints_tool/             Python constraints MCP server
```

### 9.2 Key Utilities

<details>
<summary><b>Early routing visualizer</b></summary>

- File: `utils/early_router.py`
- Function: `build_early_routes(...)`
- Produces Manhattan preview segments (layer 998/0) for visual feedback. Not DRC-clean; no PDK metal stack.

</details>

<details>
<summary><b>SPICE → analog placement problem</b></summary>

- File: `utils/spice_to_analog_problem.py`
- Function: `build_analog_problem_from_spice(...)`
- Builds a tool-neutral `analog_problem` for the C++ placer. Preferred path uses the constraints MCP (`constraints.extract`); a local parser is the fallback.

</details>

### 9.3 Viewer Commands (partial)

| Action | Argument shape | Used by |
|---|---|---|
| `draw_instances` | `{layer: "999/0", rectangles: [{x, y, w, h, name}]}` | m2 |
| `draw_routes` | `{layer: "998/0", segments: [{x1, y1, x2, y2}]}` | m2 |
| `zoom_fit` | `{}` | m4 |
| `refresh_view` | `{}` | m4 |
| `screenshot` | `{path?: string}` | m4 |
| `unlock_ui` | `{}` | `collect_outputs` |
