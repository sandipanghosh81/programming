# SANDIPAN GHOSH

**Senior Software Architect -- Device Routing, Placement & Agentic AI for Silicon**
Cupertino, CA | +1 (408) 455-0969 | sandipan.ghosh81@gmail.com | www.linkedin.com/in/sandipanghosh81 | github.com/sandipanghosh81

---

## SUMMARY

21-year EDA veteran who architects the **device auto-placement and routing engines** Apple, NVIDIA, and Intel depend on -- now extending them with **Agentic AI** for autonomous design closure. 7+ years as lead architect of Cadence's device routing and fill infrastructure across every major foundry node from 16nm to N2. Central designer of a patented **DRC-Aware Routing Grids System** that decouples routing algorithms from PDK-specific rules, cutting new-node enablement from months to weeks. Now building agentic systems that wrap these deterministic engines for fully automated memory chip assembly routing.

---

## DEVICE AUTO-PLACEMENT & ROUTING -- 7+ Years as Lead Architect

### DRC-Aware Routing Grids System (Patented -- US 11,790,147)

Designed the core architecture of Cadence's **Grid-Based Device Router (GBR)** -- a PDK-independent routing system for Virtuoso:

- **Routing Graph**: Dynamic graph representing all available routing space across all metal layers. Edges encode DRC rules (spacing, halos, EOL). Graph updates in real-time as segments and vias are created.
- **Segment Service + Via Service**: Modular DRC components that compute shadows/halos from cached constraints (minArea, minSpacing, EOL, minSpanLengthSpacing). Decoupled from routing algorithm -- enables rapid PDK enablement.
- **PDK Enablement Results**: TSMC N7 (3 months), N5 (2 months), N3 (1 month), GPDK (2 weeks) -- each successive node faster due to architecture scalability.
- **Multi-Net Resource Allocation**: Addressed congestion from greedy net-sequential routing where early nets consume lower-metal resources, blocking later nets.

### Bayesian Optimization of Routing Parameters (Internal R&D)

Led exploration of **Optuna-based hyperparameter optimization** (Python) applied to the GBR:

- Treated router as a black box -- optimized per-design routing parameters (preferred layers, via/metal costs, net ordering) using Bayesian Optimization.
- Reduced opens from 25 to 3 on representative testcases. 57% reduction in highly-resistive metal usage.
- Validated on TSMC N3, Intel 1278 -- 100 trials on 20 parallel threads, optimal parameters at trial 60-80.
- Proposed warm-start (reuse parameters across similar PDK/designs) and one-click autonomous routing.

### Device Fill, Placement & Infrastructure

- **Metal & Poly Filler Engines**: Architected Virtuoso's density fill engines -- antenna fixing (jumpers, diodes), layer density analysis, DFM compliance.
- **SADP Grid Infrastructure**: Grid models, checkers, and tools for Self-Aligned Double Patterning at 10nm/7nm.
- **Width-Spacing Patterns** (US 10,733,351): Automatic WSP generation for color-based multi-patterning track systems, handling flipped/mirrored instances.

---

## AGENTIC AI: Autonomous Memory Routing Engine

Production-scale routing engine orchestrated by multi-agent AI -- targeting 1M+ net capacity for memory chip assembly (DRAM/SRAM/Flash) at TSMC N2 / Intel 18A.

| Component | What It Does |
|:---|:---|
| **Memory Routing Architecture** (v4.8) | Block + Periphery domain specs -- bitlines, wordlines, fishbone topology (2,155 lines) |
| **Implementation Spec** (v2.0) | RouterBase hierarchy, 14-tool RoutingToolPool, EDA callbacks for Virtuoso/Innovus/OpenROAD (2,261 lines) |
| **Pin-to-Spine Tool** (P2S v1.0) | Standalone `libp2s.a` -- 39 function objects, 7-phase pipeline, zero-shorts hard constraint (1,650 lines) |
| **LangGraph Orchestrator + Meta-Optimizer** | AI brain + LLM-guided Gurobi/SciPy/DEAP solver pool (2,805 lines) |

**Key innovation**: Python/LangGraph orchestrates strategy (algorithm selection, net ordering, solver switching). C++ engine executes at microsecond-per-edge speed. Built-in DRC/LVS verification at every routing step. Zero hallucination in the compute path.

---

## CADENCE DESIGN SYSTEMS -- 21+ Years (2004 - Present)

| Role | Period | Impact |
|:---|:---|:---|
| **Senior Software Architect** | 2024 - Present | Agentic AI + device routing/fill for advanced nodes |
| **Software Architect** | 2020 - 2024 | Led 5-engineer team. DRC-Aware Routing Grids System (US 11,790,147). 5nm/3nm/2nm. |
| **Sr. Member Consulting Staff** | 2016 - 2020 | Metal/poly filler engine, antenna fixing, SADP grids for 10nm/7nm |
| **Sr. Member Consulting Staff** | 2013 - 2016 | Virtuoso Layout Editor: FinFET, Neighborhood Awareness, Motif-to-Qt |
| **Member Consulting Staff** | 2007 - 2013 | Constraint GUI (org-level award), LEF/DEF/GDS translators, 2 papers |

**Strategic Engagements**: Apple, NVIDIA, Intel, Samsung, TSMC, Qualcomm, Broadcom, AMD, Sony, MediaTek, Amazon

---

## OTHER AGENTIC AI PROJECTS

| # | Project | Stack |
|:---:|:---|:---|
| 1 | **Chat Assistant** | OpenAI Agents SDK, 14 MCP servers -- E*TRADE, Gmail, Outlook, Polygon |
| 2 | **MCP Servers** (5) | Python, OAuth2 -- E*TRADE, Gmail, Outlook, Pushover, Proxy |
| 3 | **MedExplainer** | LangGraph, 7 medical APIs -- drug journey narrator (239 tests) |
| 4 | **College Planner** | LangGraph, 6 MCP servers -- multi-agent student guidance |
| 5 | **CrewAI Portfolio** (3) | Crew AI -- stock picker, backtester, portfolio manager |
| 6 | **Optimization Suite** | Gurobi, PuLP, SciPy, DEAP, OR-Tools -- 20+ implementations |

---

## TECHNICAL PROFICIENCIES

| Domain | Technologies |
|:---|:---|
| **Device Routing** | Grid-Based Router (GBR), DRC-aware routing graphs, net ordering, congestion management, via/segment services |
| **AI/ML Agents** | LangGraph, Crew AI, AutoGen 2, OpenAI Agents SDK, MCP Architecture, Optuna (Bayesian) |
| **EDA / Silicon** | Virtuoso, SKILL, OpenAccess API, DFM, finFET/gaaFET PDKs, DEF/LEF/GDSII |
| **Optimization** | Gurobi, SciPy, PuLP, OR-Tools, DEAP (GA), Optuna, Bayesian optimization |
| **Languages** | C++, Python, SKILL, SQL |
| **Foundry Nodes** | TSMC (A14/16, N2/N2P/N3/N4/N5/N7), Intel (18A/20A/1278), Samsung (SF2/3/4/8) |

---

## CREDENTIALS

| Category | Detail |
|:---|:---|
| **Education** | B.Tech, Mechanical Engineering -- **Indian Institute of Technology (IIT), Delhi** (Class of 2002) |
| **Patents** | 7 granted (US 11790147, 10769346, 10733351, 9305133, 8612923, 8327315, 8271909) -- routing graphs, object rearrangement, multi-patterning WSPs, constraint hierarchies, neighborhood awareness |
| **Awards** | 2x Organization-Level Awards, Excellence in Innovation, Excellence in Execution, Cadence Encore Award |
| **Publications** | 2 papers presented at Cadence Technical Conference (US) |
