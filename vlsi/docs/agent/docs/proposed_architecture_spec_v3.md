# Graph-Based VLSI AI Agent Architecture Spec (v5)

This specification defines a **Distributed Mini-Orchestrator** model where subtools are exposed as independent, "smart" MCP Servers. Each server contains its own Python LangGraph sub-graph granting it the autonomy to resolve missing context by querying peer MCP servers before invoking the underlying C++ tool engine.

---

## 1. Security and Deployment Constraints

- **Dual-Model Support**: Standardized LLM APIs route sanitized planning requests to commercial cloud models (OpenAI, Anthropic, Gemini) and data-sensitive specialist loops to local on-premises GPU clusters (NVIDIA NIM, Ollama, vLLM).
- **Fully Air-Gappable**: Supports 100% on-premises deployment within the corporate LAN.

---

## 2. Distributed Modular Server Architecture

The C++ layer exposes capabilities as individual, independent MCP servers — each modular, each with defined skills.

### 2.1 Always-On Informational Servers (Auto-Start)
These start automatically alongside the Agentic Service and remain persistent:

| Server | Capabilities | Communication |
|--------|-------------|---------------|
| **Database Layer MCP** | DB Object IDs, net counts, instance queries, PMOS/NMOS device analysis, netlists, bounding boxes | Direct memory access (same process) |
| **Windows/View Layer MCP** | Active windows, focused pane, visible coordinates, zoom level, selection state | Direct memory access (same process) |
| **Documentation RAG MCP** | Full-text search, semantic retrieval, API reference lookup | In-process or IPC (may index external docs) |

### 2.2 On-Demand Subtool Servers (Started When Needed)
These are heavyweight subtools started only when the orchestrator requires them:

| Server | Capabilities | Communication |
|--------|-------------|---------------|
| **Placement MCP** | Component placement, floorplanning, legalization | Direct memory or IPC |
| **Routing MCP** | Signal routing, via insertion, DRC-aware path finding | Direct memory or IPC |
| **Editing MCP** | Design modifications, wire editing, instance property changes | Direct memory access |
| **Viewing MCP** | Zoom, pan, layer visibility, highlight, screenshot | Direct memory access |

### 2.3 Inter-Server Communication Strategy

Since most tools live in the **same process memory space**, we use a hybrid approach:

```mermaid
graph LR
    subgraph "Same EDA Process (Shared Memory)"
        direction TB
        DB["🗄️ Database MCP"]
        WIN["🖥️ Windows MCP"]
        EDIT["✏️ Editing MCP"]
        VIEW["👁️ Viewing MCP"]
        PLACE["📐 Placement MCP"]
        
        PLACE -->|"Direct C++ call<br/>(shared memory,<br/>function pointers)"| DB
        PLACE -->|"Direct C++ call"| WIN
        EDIT -->|"Direct C++ call"| DB
        VIEW -->|"Direct C++ call"| WIN
    end
    
    subgraph "Separate Process"
        ROUTE["🔀 Routing MCP<br/>(may be out-of-process)"]
    end
    
    ROUTE <-->|"IPC<br/>(MCP over HTTP<br/>or Unix socket)"| DB
    ROUTE <-->|"IPC"| WIN
    
    style DB fill:#2ecc71,color:#fff
    style WIN fill:#2ecc71,color:#fff
    style PLACE fill:#e67e22,color:#fff
    style ROUTE fill:#e67e22,color:#fff
    style EDIT fill:#3498db,color:#fff
    style VIEW fill:#3498db,color:#fff
```

**Rule**: If the calling server and the target server share the same process address space, use **direct C++ function calls** through the internal tool registry (fast, zero-copy, no serialization). If the target is out-of-process, fall back to **MCP over HTTP/Unix sockets** (IPC). Every MCP server exposes the same MCP protocol interface regardless — the transport is an implementation detail abstracted behind a unified `ToolClient` interface.

---

## 3. Python LangGraph as Universal Orchestrator

**All orchestration logic — both the master graph and every mini-orchestrator inside each subtool MCP server — runs in Python LangGraph.** The C++ layer is purely the execution substrate: it exposes capabilities, executes commands, and returns results. It never makes LLM calls or routing decisions.

```mermaid
graph TB
    subgraph "Python LangGraph Process"
        direction TB
        MasterGraph["🎯 Master Graph<br/>(Router, Planner, Verifier)"]
        
        MiniPlacement["📐 Placement Sub-Graph<br/>(LangGraph)"]
        MiniRouting["🔀 Routing Sub-Graph<br/>(LangGraph)"]
        MiniEditing["✏️ Editing Sub-Graph<br/>(LangGraph)"]
        
        MasterGraph -->|"delegate"| MiniPlacement
        MasterGraph -->|"delegate"| MiniRouting
        MasterGraph -->|"delegate"| MiniEditing
    end
    
    subgraph "C++ EDA Process (Execution Only)"
        direction TB
        DBServer["🗄️ DB MCP Server"]
        WinServer["🖥️ Windows MCP Server"]
        PlaceTool["📐 Placement Engine"]
        RouteTool["🔀 Routing Engine"]
        EditTool["✏️ Editing Engine"]
    end
    
    MiniPlacement <-->|"MCP"| DBServer
    MiniPlacement <-->|"MCP"| WinServer
    MiniPlacement <-->|"MCP"| PlaceTool
    
    MiniRouting <-->|"MCP"| DBServer
    MiniRouting <-->|"MCP"| RouteTool
    
    MiniEditing <-->|"MCP"| DBServer
    MiniEditing <-->|"MCP"| EditTool
    
    subgraph "LLM Endpoints"
        LLM["Commercial or Local LLM"]
    end
    
    MasterGraph <-->|"REST"| LLM
    MiniPlacement <-->|"REST"| LLM
    MiniRouting <-->|"REST"| LLM
    
    style MasterGraph fill:#e67e22,color:#fff
    style MiniPlacement fill:#f39c12,color:#fff
    style MiniRouting fill:#f39c12,color:#fff
    style MiniEditing fill:#f39c12,color:#fff
    style DBServer fill:#2ecc71,color:#fff
    style WinServer fill:#2ecc71,color:#fff
    style LLM fill:#9b59b6,color:#fff
```

---

## 4. C++ MCP Server Internal Architecture

Each C++ MCP Server follows a common internal pattern. The server itself is a **thin wrapper** — it receives MCP protocol requests, dispatches to the appropriate C++ function, handles thread-affinity constraints, and returns structured results.

### 4.1 Single C++ MCP Server — Internal Flow

```mermaid
flowchart TD
    Req([🔵 Incoming MCP Request]) --> Parse["Parse MCP JSON-RPC<br/>(tool name + params)"]
    
    Parse --> Registry{"Tool Registry<br/>Lookup"}
    
    Registry -->|"Found"| ThreadCheck{"Thread Affinity<br/>Required?"}
    Registry -->|"Not Found"| Err404["Return error:<br/>tool not found"]
    
    ThreadCheck -->|"No — safe on any thread"| DirectCall["Direct C++ Function Call<br/>(current I/O thread)"]
    ThreadCheck -->|"Yes — must run on<br/>GUI main thread"| QtDispatch["Qt::QueuedConnection<br/>dispatch to main thread"]
    
    QtDispatch --> WaitSignal["Block I/O thread<br/>Wait for completion signal"]
    WaitSignal --> MainExec["Execute on Main Thread<br/>(GUI-safe context)"]
    MainExec --> MainResult["Emit result signal"]
    MainResult --> Collect["Collect result on I/O thread"]
    
    DirectCall --> Collect
    
    Collect --> Serialize["Serialize result to<br/>MCP JSON-RPC response"]
    Serialize --> Resp([🟢 MCP Response])
    
    style Req fill:#3498db,color:#fff
    style Resp fill:#27ae60,color:#fff
    style QtDispatch fill:#e74c3c,color:#fff
    style MainExec fill:#e74c3c,color:#fff
    style DirectCall fill:#2ecc71,color:#fff
```

### 4.2 Database Layer MCP Server — Tools and Dispatch

```mermaid
flowchart TD
    MCPIn([🔵 MCP Request]) --> Router{"Route by<br/>tool name"}
    
    Router -->|"count_nets"| CountNets["countNets()<br/>→ int"]
    Router -->|"count_instances"| CountInst["countInstances()<br/>→ int"]
    Router -->|"count_devices"| CountDev["countDevices(type)<br/>→ {pmos: N, nmos: M}"]
    Router -->|"get_object_ids"| GetIDs["getObjectIDs(filter)<br/>→ list of DB IDs"]
    Router -->|"get_netlist"| GetNetlist["getNetlist(cell)<br/>→ SPICE netlist string"]
    Router -->|"get_bounding_box"| GetBBox["getBoundingBox(objId)<br/>→ {x1,y1,x2,y2}"]
    Router -->|"query_hierarchy"| QueryHier["queryHierarchy(path)<br/>→ tree structure"]
    
    CountNets & CountInst & CountDev --> DirectMem["Direct Memory Read<br/>(design database pointer)"]
    GetIDs & GetNetlist & GetBBox & QueryHier --> DirectMem
    
    DirectMem --> Result["Format MCP Response"]
    Result --> MCPOut([🟢 MCP Response])
    
    style MCPIn fill:#3498db,color:#fff
    style MCPOut fill:#27ae60,color:#fff
    style DirectMem fill:#2ecc71,color:#fff
```

### 4.3 Windows/View Layer MCP Server — Tools and Dispatch

```mermaid
flowchart TD
    MCPIn([🔵 MCP Request]) --> Router{"Route by<br/>tool name"}
    
    Router -->|"list_windows"| ListWin["listOpenWindows()<br/>→ list of window descriptors"]
    Router -->|"get_active_window"| GetActive["getActiveWindow()<br/>→ window ID + type"]
    Router -->|"get_viewport"| GetVP["getViewport(winId)<br/>→ {x,y,width,height,zoom}"]
    Router -->|"get_selection"| GetSel["getSelection(winId)<br/>→ list of selected obj IDs"]
    Router -->|"get_layer_visibility"| GetLayers["getLayerVisibility(winId)<br/>→ layer → bool map"]
    
    ListWin & GetActive --> GUIThread["Must dispatch to<br/>GUI Main Thread<br/>(Qt thread affinity)"]
    GetVP & GetSel & GetLayers --> GUIThread
    
    GUIThread --> QtSignal["Qt signal/slot<br/>cross-thread dispatch"]
    QtSignal --> Result["Format MCP Response"]
    Result --> MCPOut([🟢 MCP Response])
    
    style MCPIn fill:#3498db,color:#fff
    style MCPOut fill:#27ae60,color:#fff
    style GUIThread fill:#e74c3c,color:#fff
```

### 4.4 Placement MCP Server — Tools, Peer Queries, and Iteration

This is an **On-Demand** server. Its Python LangGraph sub-graph orchestrates a multi-step workflow that queries peer servers before invoking the C++ engine.

```mermaid
sequenceDiagram
    participant Master as 🎯 Master LangGraph
    participant PlaceSG as 📐 Placement Sub-Graph<br/>(Python LangGraph)
    participant LLM as 🤖 LLM
    participant PlaceMCP as 📐 Placement MCP Server<br/>(C++)
    participant DBMCP as 🗄️ Database MCP Server<br/>(C++, Always-On)
    participant WinMCP as 🖥️ Windows MCP Server<br/>(C++, Always-On)
    
    Master->>PlaceSG: "Place decoupling caps near power pins"
    
    Note over PlaceSG: Self-Diagnosis Node
    PlaceSG->>LLM: "What data do I need to place decap cells?"
    LLM-->>PlaceSG: "Need: cell names, power pin locations,<br/>placement region, current viewport"
    
    Note over PlaceSG: Sibling Query Phase
    PlaceSG->>DBMCP: get_object_ids(filter="decap_cell")
    DBMCP-->>PlaceSG: [id_101, id_102, id_103]
    
    PlaceSG->>DBMCP: get_bounding_box(obj="power_domain_A")
    DBMCP-->>PlaceSG: {x1:0, y1:0, x2:500, y2:300}
    
    PlaceSG->>WinMCP: get_active_window()
    WinMCP-->>PlaceSG: {winId: "layout_1", zoom: 0.5}
    
    Note over PlaceSG: Synthesis & Execution Node
    PlaceSG->>LLM: "Given these IDs and region,<br/>generate placement parameters"
    LLM-->>PlaceSG: {cells: [...], coords: [...], constraints: [...]}
    
    PlaceSG->>PlaceMCP: place_cells({cells, coords, constraints})
    PlaceMCP-->>PlaceSG: {status: "done", violations: 3}
    
    Note over PlaceSG: Local Verification Node
    PlaceSG->>PlaceMCP: run_drc(region="power_domain_A")
    PlaceMCP-->>PlaceSG: {violations: [{type: "overlap", ...}]}
    
    Note over PlaceSG: Iteration — violations detected
    PlaceSG->>LLM: "3 overlap violations. Adjust placement."
    LLM-->>PlaceSG: {adjusted_coords: [...]}
    
    PlaceSG->>PlaceMCP: place_cells({cells, adjusted_coords})
    PlaceMCP-->>PlaceSG: {status: "done", violations: 0}
    
    PlaceSG->>PlaceMCP: run_drc(region="power_domain_A")
    PlaceMCP-->>PlaceSG: {violations: []}
    
    Note over PlaceSG: Converged — return to master
    PlaceSG-->>Master: {success: true, cells_placed: 3, iterations: 2}
```

---

## 5. Master Orchestration Graph (Complete)

```mermaid
graph TD
    START(("▶ Start")) --> Router["🔀 Router Node"]
    
    Router -->|"design query<br/>(how many nets?)"| QueryExec["⚡ Query Executor<br/>(Always-On DB + RAG)"]
    Router -->|"simple action<br/>(zoom in)"| SimpleExec["⚡ Simple Executor"]
    Router -->|"complex task"| Planner["📋 Planner Node"]
    Router -->|"ambiguous"| HumanClarify["👤 Clarify<br/>(Interrupt)"]
    
    HumanClarify --> Router
    
    Planner --> PlanApproval["👤 Plan Approval<br/>(Interrupt)"]
    PlanApproval -->|"approved"| Lifecycle["⚙️ Tool Lifecycle<br/>Start On-Demand Servers"]
    PlanApproval -->|"modify"| Planner
    PlanApproval -->|"cancel"| END_NODE(("⏹ End"))
    
    Lifecycle --> MasterExec["⚡ Master Executor"]
    
    MasterExec -->|"P&R task"| PlaceSG["📐 Placement<br/>Sub-Graph"]
    MasterExec -->|"Routing task"| RouteSG["🔀 Routing<br/>Sub-Graph"]
    MasterExec -->|"Edit task"| EditSG["✏️ Editing<br/>Sub-Graph"]
    
    SimpleExec -->|"MCP"| ViewMCP["👁️ Viewing MCP"]
    SimpleExec -->|"MCP"| EditMCP["✏️ Editing MCP"]
    
    subgraph "Iterative Sub-Graphs (Python LangGraph)"
        PlaceSG <-->|"MCP"| DBMCP["🗄️ DB MCP"]
        PlaceSG <-->|"MCP"| WinMCP["🖥️ Win MCP"]
        PlaceSG <-->|"MCP"| PlaceMCP["📐 Place Engine"]
        
        RouteSG <-->|"MCP"| DBMCP2["🗄️ DB MCP"]
        RouteSG <-->|"MCP"| RouteMCP["🔀 Route Engine"]
    end
    
    PlaceSG --> MasterVerify["✅ Master Verifier"]
    RouteSG --> MasterVerify
    EditSG --> MasterVerify
    QueryExec --> MasterVerify
    SimpleExec --> MasterVerify
    
    MasterVerify -->|"passed + more"| MasterExec
    MasterVerify -->|"all done"| Memory["🧠 Memory"]
    MasterVerify -->|"failed"| Replan["🔄 Re-Planner"]
    
    Replan --> MasterExec
    Replan -->|"max retries"| HumanEsc["👤 Escalate"]
    HumanEsc --> Replan
    
    Memory --> END_NODE
    
    style Router fill:#4a90d9,color:#fff
    style Planner fill:#7b68ee,color:#fff
    style MasterExec fill:#e67e22,color:#fff
    style MasterVerify fill:#27ae60,color:#fff
    style Replan fill:#f39c12,color:#fff
    style PlaceSG fill:#f39c12,color:#fff
    style RouteSG fill:#f39c12,color:#fff
    style HumanClarify fill:#e74c3c,color:#fff
    style PlanApproval fill:#e74c3c,color:#fff
    style DBMCP fill:#2ecc71,color:#fff
    style WinMCP fill:#2ecc71,color:#fff
```

---

## 6. Resolving Lifecycle and Interoperability

| Concern | Resolution |
|---------|-----------|
| **Master stays lightweight** | Python LangGraph master only routes, plans, and verifies. Domain logic lives in sub-graphs. |
| **C++ is execution-only** | C++ servers never call LLMs or make routing decisions. They expose pure functions via MCP. |
| **Inter-server queries are fast** | Same-process servers use direct memory calls. Out-of-process servers use MCP/IPC. All behind a unified `ToolClient` interface. |
| **Sub-graphs are autonomous** | Each domain sub-graph independently queries DB, Windows, and RAG servers to resolve its own parameters before acting. |
| **Modularity preserved** | C++ engineers define what tools their server exposes. Python engineers define how those tools are orchestrated. Clean boundary. |

---

## 7. End-to-End Example: "Change the aspect ratio of the placement and rearrange the instances"

This traces every call, every boundary crossing, and every iteration loop for a real user query.

### 7.1 Annotated Flow — Python vs C++ Boundaries, LLM Interactions, and Command Assembly

> **Legend**: 🟠 Orange = Python (LangGraph, out-of-process) · 🟢 Green = C++ (in-tool memory) · 🔵 Blue = LLM call · 🔴 Red = Human interrupt

```mermaid
flowchart TD
    subgraph "C++ EDA Process (In-Tool Memory)"
        ChatPanel["🟢 Chat Panel (C++/Qt)<br/>━━━━━━━━━━━━━━<br/>Captures: user text,<br/>active window ID,<br/>selection state"]
        
        DBMCP["🟢 Database MCP Server<br/>━━━━━━━━━━━━━━<br/>Always-On · Direct Memory<br/>Reads design DB pointers"]
        
        WinMCP["🟢 Windows MCP Server<br/>━━━━━━━━━━━━━━<br/>Always-On · GUI Thread<br/>Reads Qt widget state"]
        
        PlaceMCP["🟢 Placement MCP Server<br/>━━━━━━━━━━━━━━<br/>On-Demand · Executes C++<br/>placement engine functions"]
    end
    
    subgraph "Python Orchestrator (Out-of-Process, Docker)"
        WSServer["🟠 WebSocket Server<br/>━━━━━━━━━━━━━━<br/>Receives: {text, context}"]
        
        Router["🟠🔵 Router Node<br/>━━━━━━━━━━━━━━<br/>SENDS to LLM:<br/>· user query text<br/>· design summary from DB MCP<br/>· list of registered agents<br/>━━━━━━━━━━━━━━<br/>EXPECTS back:<br/>· {route, agent_type, confidence}<br/>━━━━━━━━━━━━━━<br/>Here: route=complex, agent=placement"]
        
        Planner["🟠🔵 Planner Node<br/>━━━━━━━━━━━━━━<br/>SENDS to LLM:<br/>· user query + route decision<br/>· available tool capabilities<br/>━━━━━━━━━━━━━━<br/>EXPECTS back:<br/>· ordered list of plan steps<br/>━━━━━━━━━━━━━━<br/>Here: 4 steps returned"]
        
        PlanApproval["🟠🔴 Plan Approval<br/>━━━━━━━━━━━━━━<br/>interrupt() → shows plan<br/>Waits for engineer response<br/>Engineer adds: ratio=1.5:1"]
        
        Lifecycle["🟠 Tool Lifecycle Coordinator<br/>━━━━━━━━━━━━━━<br/>Spawns Placement MCP Server<br/>Waits for {status: ready}"]
        
        subgraph "Placement Sub-Graph (Python LangGraph)"
            SelfDiag["🟠🔵 Self-Diagnosis Node<br/>━━━━━━━━━━━━━━<br/>SENDS to LLM:<br/>· task description<br/>· available peer servers<br/>━━━━━━━━━━━━━━<br/>EXPECTS back:<br/>· list of required data fields<br/>━━━━━━━━━━━━━━<br/>Here: need instance IDs,<br/>floorplan bounds, pin locs, viewport"]
            
            Collect["🟠 Parameter Collection<br/>━━━━━━━━━━━━━━<br/>Queries peer C++ servers:<br/>· DB MCP → get_object_ids<br/>· DB MCP → get_bounding_box<br/>· DB MCP → get_object_ids(pins)<br/>· Win MCP → get_active_window<br/>· Win MCP → get_viewport<br/>━━━━━━━━━━━━━━<br/>Collects into state dict:<br/>{ids:[...], bbox:{...},<br/>pins:[...], viewport:{...}}"]
            
            Synthesize["🟠🔵 Command Synthesis Node<br/>━━━━━━━━━━━━━━<br/>SENDS to LLM:<br/>· collected params from state<br/>· target aspect ratio (1.5:1)<br/>· current bbox (1000x800)<br/>━━━━━━━━━━━━━━<br/>EXPECTS back:<br/>· new floorplan dimensions<br/>· placement strategy + constraints<br/>━━━━━━━━━━━━━━<br/>ASSEMBLES C++ commands:<br/>set_floorplan(w=1095,h=730)<br/>place_instances(strategy=<br/>global_reflow, density=0.7)"]
            
            Execute["🟠 Execution Node<br/>━━━━━━━━━━━━━━<br/>Sends assembled commands<br/>to C++ Placement MCP Server<br/>via MCP over HTTP"]
            
            Verify["🟠🔵 Local Verifier<br/>━━━━━━━━━━━━━━<br/>Calls: PlaceMCP.run_drc()<br/>━━━━━━━━━━━━━━<br/>If violations > 0:<br/>SENDS to LLM:<br/>· violation count + types<br/>· previous strategy<br/>EXPECTS back:<br/>· adjusted parameters<br/>ASSEMBLES new command:<br/>legalize_placement(margin=0.05)<br/>━━━━━━━━━━━━━━<br/>Loops until violations=0<br/>or max_retries hit"]
        end
        
        MasterVerify["🟠🔵 Master Verifier<br/>━━━━━━━━━━━━━━<br/>SENDS to LLM:<br/>· sub-graph result summary<br/>· original user intent<br/>EXPECTS back:<br/>· {passed: bool, reason: str}"]
        
        Memory["🟠 Memory Node<br/>━━━━━━━━━━━━━━<br/>Stores to PostgreSQL:<br/>· successful approach<br/>· parameters used"]
    end
    
    subgraph "LLM Endpoint"
        LLM["🔵 Commercial or Local LLM<br/>━━━━━━━━━━━━━━<br/>OpenAI / Anthropic / Gemini<br/>or vLLM / NIM / Ollama"]
    end
    
    ChatPanel -->|"WebSocket:<br/>{text, activeWindow,<br/>selectionState}"| WSServer
    WSServer --> Router
    
    Router -->|"MCP call"| DBMCP
    Router -->|"LLM call"| LLM
    Router -->|"route=complex"| Planner
    
    Planner -->|"LLM call"| LLM
    Planner --> PlanApproval
    
    PlanApproval -->|"interrupt →<br/>WebSocket → Chat"| ChatPanel
    PlanApproval --> Lifecycle
    
    Lifecycle -->|"spawn"| PlaceMCP
    Lifecycle --> SelfDiag
    
    SelfDiag -->|"LLM call"| LLM
    SelfDiag --> Collect
    
    Collect -->|"MCP calls"| DBMCP
    Collect -->|"MCP calls"| WinMCP
    Collect --> Synthesize
    
    Synthesize -->|"LLM call"| LLM
    Synthesize --> Execute
    
    Execute -->|"MCP commands"| PlaceMCP
    Execute --> Verify
    
    Verify -->|"MCP: run_drc"| PlaceMCP
    Verify -->|"LLM call<br/>(on failure)"| LLM
    Verify -->|"violations > 0"| Execute
    Verify -->|"violations = 0"| MasterVerify
    
    MasterVerify -->|"LLM call"| LLM
    MasterVerify --> Memory
    
    Memory -->|"WebSocket:<br/>final response"| ChatPanel
    
    style ChatPanel fill:#2ecc71,color:#fff
    style DBMCP fill:#2ecc71,color:#fff
    style WinMCP fill:#2ecc71,color:#fff
    style PlaceMCP fill:#2ecc71,color:#fff
    style WSServer fill:#e67e22,color:#fff
    style Router fill:#e67e22,color:#fff
    style Planner fill:#7b68ee,color:#fff
    style PlanApproval fill:#e74c3c,color:#fff
    style Lifecycle fill:#e67e22,color:#fff
    style SelfDiag fill:#f39c12,color:#fff
    style Collect fill:#f39c12,color:#fff
    style Synthesize fill:#f39c12,color:#fff
    style Execute fill:#f39c12,color:#fff
    style Verify fill:#f39c12,color:#fff
    style MasterVerify fill:#27ae60,color:#fff
    style Memory fill:#e67e22,color:#fff
    style LLM fill:#9b59b6,color:#fff
```

### 7.1.1 LLM Interaction Summary

| Node | Sends to LLM | Expects Back | Uses Result To |
|------|-------------|-------------|----------------|
| **Router** | User text + design summary + agent list | `{route, agent_type, confidence}` | Decide: simple exec vs planner vs clarify |
| **Planner** | User query + route decision + tool capabilities | Ordered list of plan steps | Present plan for approval |
| **Self-Diagnosis** | Task description + available peer servers | List of required data fields | Know which peer MCP servers to query |
| **Command Synthesis** | Collected params + target constraints | New dimensions + strategy + placement params | **Assemble the actual C++ MCP commands**: `set_floorplan(w=1095, h=730)`, `place_instances(strategy="global_reflow", density=0.7)` |
| **Local Verifier** | Violation count + types + previous strategy | Adjusted parameters | **Re-assemble corrected commands**: `legalize_placement(instances=[...], margin=0.05)` or `move_instances(inst_891={dx:0.1})` |
| **Master Verifier** | Sub-graph result summary + original intent | `{passed: bool, reason}` | Decide: done vs re-plan vs escalate |

### 7.1.2 Command Assembly Pattern

The **Command Synthesis Node** is the critical translation layer. It does NOT pass raw user text to C++. Instead:

```
1. COLLECT from peers:     DB MCP → ids, bbox, pins
                           Win MCP → viewport, window ID

2. REASON via LLM:         "Given 1000x800 bbox, target 1.5:1 →
                            new dims = 1095x730.
                            1247 instances, density target 0.7,
                            strategy = global_reflow"

3. ASSEMBLE MCP command:   place_instances({
                             instances: [inst_001..inst_1247],
                             region: {w:1095, h:730},
                             strategy: "global_reflow",
                             constraints: {density: 0.7},
                             fixed_pins: [pin_VDD, pin_VSS, ...]
                           })

4. DISPATCH to C++:        → Placement MCP Server executes
                             the fully-formed command
```

### 7.2 Detailed Call Sequence

```mermaid
sequenceDiagram
    participant Eng as 👤 Engineer
    participant Chat as Chat Panel<br/>(C++/Qt)
    participant WS as WebSocket Server<br/>(Python)
    participant Router as 🔀 Router Node<br/>(LangGraph)
    participant LLM as 🤖 LLM
    participant Planner as 📋 Planner Node<br/>(LangGraph)
    participant Lifecycle as ⚙️ Lifecycle Coordinator
    participant PlaceSG as 📐 Placement Sub-Graph<br/>(LangGraph)
    participant DBMCP as 🗄️ Database MCP<br/>(C++, Always-On)
    participant WinMCP as 🖥️ Windows MCP<br/>(C++, Always-On)
    participant PlaceMCP as 📐 Placement MCP<br/>(C++, On-Demand)

    Note over Eng, Chat: ━━ PHASE 1: User Input ━━
    Eng->>Chat: "Change the aspect ratio of the<br/>placement and rearrange the instances"
    Chat->>WS: WebSocket message:<br/>{text: "...", context: {activeWindow: "layout_1"}}

    Note over WS, Router: ━━ PHASE 2: Routing ━━
    WS->>Router: Start graph execution
    Router->>DBMCP: get_design_summary()
    DBMCP-->>Router: {instances: 1247, nets: 3891, area: ...}
    Router->>LLM: "Classify: user wants to change aspect<br/>ratio and re-place instances.<br/>Design has 1247 instances."
    LLM-->>Router: {route: "complex", agent: "placement",<br/>confidence: 0.96}

    Note over Router, Planner: ━━ PHASE 3: Planning ━━
    Router->>Planner: Route to planner
    Planner->>LLM: "Decompose this task into steps"
    LLM-->>Planner: Plan:<br/>1. Query current aspect ratio & placement state<br/>2. Set new aspect ratio<br/>3. Re-place all instances with new constraints<br/>4. Run DRC verification

    Note over Planner, Eng: ━━ PHASE 4: Human Approval ━━
    Planner->>WS: interrupt(plan)
    WS->>Chat: Show plan to engineer
    Chat->>Eng: Display: "I'll change the aspect ratio<br/>and re-place 1247 instances. Proceed?"
    Eng->>Chat: "Yes, use 1.5:1 ratio"
    Chat->>WS: {approved: true, params: {ratio: "1.5:1"}}
    WS->>Planner: Resume with user response

    Note over Planner, Lifecycle: ━━ PHASE 5: Server Lifecycle ━━
    Planner->>Lifecycle: Start placement server
    Lifecycle->>PlaceMCP: spawn / activate
    PlaceMCP-->>Lifecycle: {status: "ready", port: 8082}

    Note over Lifecycle, PlaceSG: ━━ PHASE 6: Placement Sub-Graph Execution ━━
    Lifecycle->>PlaceSG: Execute placement task<br/>{goal: "aspect ratio 1.5:1, re-place all"}

    Note over PlaceSG: Self-Diagnosis Node
    PlaceSG->>LLM: "I need to change aspect ratio to 1.5:1<br/>and re-place instances. What data do I need?"
    LLM-->>PlaceSG: "Need: all instance IDs, current floorplan<br/>bounds, pin locations, active window"

    Note over PlaceSG, DBMCP: ━━ Sibling Query Phase ━━
    PlaceSG->>DBMCP: get_object_ids(filter="all_instances")
    DBMCP-->>PlaceSG: [inst_001 .. inst_1247]

    PlaceSG->>DBMCP: get_bounding_box(obj="top_cell")
    DBMCP-->>PlaceSG: {x1:0, y1:0, x2:1000, y2:800}

    PlaceSG->>DBMCP: get_object_ids(filter="fixed_pins")
    DBMCP-->>PlaceSG: [pin_VDD, pin_VSS, pin_CLK, ...]

    PlaceSG->>WinMCP: get_active_window()
    WinMCP-->>PlaceSG: {winId: "layout_1", type: "layout"}

    PlaceSG->>WinMCP: get_viewport(winId="layout_1")
    WinMCP-->>PlaceSG: {x:0, y:0, w:1000, h:800, zoom:1.0}

    Note over PlaceSG, PlaceMCP: ━━ Execution: Change Aspect Ratio ━━
    PlaceSG->>LLM: "Current bounds 1000x800 (1.25:1).<br/>Target 1.5:1. Calculate new dimensions."
    LLM-->>PlaceSG: {new_width: 1095, new_height: 730}

    PlaceSG->>PlaceMCP: set_floorplan({w:1095, h:730, ratio:"1.5:1"})
    PlaceMCP-->>PlaceSG: {status: "floorplan_updated"}

    Note over PlaceSG, PlaceMCP: ━━ Execution: Re-Place Instances ━━
    PlaceSG->>LLM: "Generate placement constraints for<br/>1247 instances in 1095x730 region<br/>with fixed pins at [...]"
    LLM-->>PlaceSG: {strategy: "global_reflow",<br/>constraints: {density_target: 0.7, ...}}

    PlaceSG->>PlaceMCP: place_instances({instances: "all",<br/>strategy: "global_reflow",<br/>constraints: {density: 0.7}})
    PlaceMCP-->>PlaceSG: {placed: 1247, violations: 12,<br/>elapsed: "4.2s"}

    Note over PlaceSG: ━━ Local Verification (Iteration 1) ━━
    PlaceSG->>PlaceMCP: run_drc(region="full_chip")
    PlaceMCP-->>PlaceSG: {violations: 12,<br/>details: [{type:"overlap", inst:"inst_034",...},<br/>{type:"spacing", inst:"inst_891",...}, ...]}

    Note over PlaceSG: ⚠️ 12 violations — iterate
    PlaceSG->>LLM: "12 DRC violations after re-placement.<br/>Types: 8 overlap, 4 spacing.<br/>Adjust strategy."
    LLM-->>PlaceSG: {strategy: "incremental_legalize",<br/>overlap_margin: 0.05,<br/>target_instances: [inst_034, inst_891, ...]}

    PlaceSG->>PlaceMCP: legalize_placement(<br/>{instances: [inst_034, inst_891, ...],<br/>margin: 0.05})
    PlaceMCP-->>PlaceSG: {adjusted: 12, violations: 2}

    Note over PlaceSG: ━━ Local Verification (Iteration 2) ━━
    PlaceSG->>PlaceMCP: run_drc(region="full_chip")
    PlaceMCP-->>PlaceSG: {violations: 2,<br/>details: [{type:"spacing", inst:"inst_891",...},<br/>{type:"spacing", inst:"inst_892",...}]}

    PlaceSG->>LLM: "2 remaining spacing violations<br/>on inst_891 and inst_892. Fix."
    LLM-->>PlaceSG: {action: "nudge",<br/>inst_891: {dx: 0.1, dy: 0},<br/>inst_892: {dx: -0.05, dy: 0.1}}

    PlaceSG->>PlaceMCP: move_instances(<br/>{inst_891: {dx:0.1}, inst_892: {dx:-0.05, dy:0.1}})
    PlaceMCP-->>PlaceSG: {moved: 2, violations: 0}

    Note over PlaceSG: ━━ Local Verification (Iteration 3) ━━
    PlaceSG->>PlaceMCP: run_drc(region="full_chip")
    PlaceMCP-->>PlaceSG: {violations: 0} ✅

    Note over PlaceSG: ━━ Converged after 3 iterations ━━

    Note over PlaceSG, WinMCP: ━━ PHASE 7: Update View ━━
    PlaceSG->>WinMCP: refresh_viewport(winId="layout_1")
    PlaceSG->>WinMCP: zoom_fit(winId="layout_1")

    Note over PlaceSG, Eng: ━━ PHASE 8: Return to Master ━━
    PlaceSG-->>WS: {success: true,<br/>aspect_ratio: "1.5:1",<br/>instances_placed: 1247,<br/>drc_iterations: 3,<br/>final_violations: 0}

    WS->>Chat: Stream final response
    Chat->>Eng: "Done! Changed aspect ratio to 1.5:1<br/>and re-placed all 1,247 instances.<br/>DRC clean after 3 iterations<br/>(12 → 2 → 0 violations)."
```

### 7.3 Call Boundary Summary

| Phase | Where | What Happens |
|-------|-------|-------------|
| **1. User Input** | C++ Chat Panel → WebSocket | Query + GUI context sent to Python |
| **2. Routing** | Python LangGraph + LLM | Classified as "complex / placement" |
| **3. Planning** | Python LangGraph + LLM | Decomposed into 4 ordered steps |
| **4. Human Approval** | Python → C++ → Engineer | `interrupt()` pauses graph, shows plan |
| **5. Server Lifecycle** | Python → C++ | On-demand Placement MCP server started |
| **6. Sub-Graph Execution** | Python LangGraph ↔ C++ MCP Servers | Sub-graph queries DB + Windows peers, invokes Placement engine, iterates DRC loop (3 cycles: 12→2→0 violations) |
| **7. Update View** | Python → C++ Windows MCP | Refresh + zoom-fit the layout window |
| **8. Response** | Python → WebSocket → C++ Chat Panel | Final summary streamed to engineer |

---

## 8. C++ Algorithmic Core Memory Management (A* Maze Router)

While the Python/MCP boundary handles orchestration, the pure performance engine relies on strict C++ memory management within the `DetailedGridRouter` beneath the Python wrappers.

### 8.1 The "Whiteboard" Analogy for Boost::Pool
In physical VLSI routing, an A* path search can easily expand millions of nodes across the unified 3D grid graph. 

Imagine organizing a massive library by handing out a brand-new notebook (an OS memory allocation, i.e., `new Node()`) to every single patron (an A* search path) who enters, and then throwing the notebook in the trash (`delete Node()`) when they leave. The system would collapse under the administrative overhead of allocating and deallocating memory.

To solve this, the **A* Grid Router implements a `boost::object_pool`**. This acts like a stack of reusable whiteboards. When a routing thread needs to expand an edge, it quickly grabs a whiteboard from the pre-allocated pool, weights the new $G + H$ costs, and places it in the `std::priority_queue`. Once the route converges, the whole stack of whiteboards is wiped clean and reused for the next net in $O(1)$ time. This completely eliminates dynamic heap allocations on the hot path, ensuring deterministic, ultra-fast routing execution.

### 8.2 The "Traffic Cop and Toll Booth" Analogy for PathFinder
The core engine relies on the `NegotiatedRoutingLoop` and `HistoryCostUpdater` to resolve shorts and overlaps.

* **NegotiatedRoutingLoop (The Traffic Cop)**: Imagine 10,000 drivers (nets) who all want to take the exact same highway at rush hour. If they drive blindly, they crash. The Negotiated Routing Loop acts as a Master Traffic Cop managing bounds. It lets everyone drive, sees exactly where the crashes (DRC shorts) happen, and rips up the crashed cars, forcing them to re-route.
* **HistoryCostUpdater (The Toll Booth Operator)**: Whenever cars crash on a specific highway lane, the Traffic Cop tells the Toll Booth Operator to permanently increase the toll price for that specific lane for all future passes. On the next round, the A* drivers see the expensive toll and naturally route themselves around the contention point. This repeats until 0 crashes occur.

---

## 9. Modern C++ 17/23 Concepts & Analogies

Throughout the V3 architecture, several bleeding-edge C++ features are heavily utilized for performance and safety.

### 9.1 `[[nodiscard]]` (The Unopened Mail Analogy)
Imagine a postman hands you a highly important certified letter, and you immediately throw it into the trash without opening it. The post office would trigger an alarm! 
`[[nodiscard]]` acts as this alarm. It mathematically forces the C++ compiler to throw a warning if an EDA function (like `route_nets()`) generates a complex route, and the programmer dangerously forgets to capture the result in a variable to use it.

### 9.2 `std::expected<T, E>` (The Package Delivery Analogy)
When you open a sealed package, you either find your physical item inside (the `value`), **OR** you find an apology slip stating why it couldn't be delivered (the `error`). You cannot have both, and you cannot have neither. 
Introduced in C++23, `std::expected` forces the API to return either a successful `RoutedPath` OR a strict `RouterError`. This mathematically forces the programmer to handle failure states rather than blindly relying on loose `bool` checks.

### 9.3 `[[likely]]` & `[[unlikely]]` (The Train Switch Analogy)
In a train system, if 99% of trains go straight and 1% turn left, operators will permanently lock the physical tracks to "straight" so trains never have to slow down. If a left-turning train comes, the track has to explicitly halt and switch, creating a delay.
`[[likely]]` instructs the physical CPU hardware to branch-predict and pre-load the "straight path" instructions right into the L1 CPU Cache. In our heavy A* maze router, we label standard matrix expansions as `[[likely]]` to drastically increase pathfinding speeds.

### 9.4 `std::span<T>` (The Glass Window Analogy)
If your colleague needs to read one specific paragraph from your 10,000-page book, photocopying the entire book to hand to them is incredibly wasteful. Instead, you just place a small glass framing window over that exact paragraph. 
`std::span` is a zero-copy pointer window that looks directly into an existing array in memory (such as a subset array of `NetIds`), allowing sub-functions to read the data without triggering massive memory duplication via pass-by-value.
