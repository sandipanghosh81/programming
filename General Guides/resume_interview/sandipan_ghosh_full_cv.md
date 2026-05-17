# SANDIPAN GHOSH

**Senior Software Architect -- Device Routing, Placement & Agentic AI for Silicon**
Cupertino, CA | +1 (408) 455-0969 | sandipan.ghosh81@gmail.com | www.linkedin.com/in/sandipanghosh81 | github.com/sandipanghosh81

---

## PROFESSIONAL SUMMARY

Senior Software Architect with **21+ years** of EDA and physical design experience at Cadence Design Systems, including **7+ years as lead architect of device auto-placement and routing** infrastructure. Central designer of a patented **DRC-Aware Routing Grids System** (US 11,790,147) that decouples routing algorithms from PDK-specific DRC rules, cutting new-node enablement from months to weeks across TSMC N3/N5/N7, Intel 1278, and Samsung nodes. Architect of "Golden" layout engines deployed at Apple, NVIDIA, Intel, Samsung, TSMC, and Qualcomm. Now extending these deterministic engines with **Agentic AI** for fully autonomous memory chip assembly routing -- a production-scale C++ engine orchestrated by LangGraph targeting 1M+ net capacity.

**What makes me different**: I am one of the few engineers in the industry who has both **designed and shipped production routing infrastructure** at Cadence for 7+ years **and** built production-grade Agentic AI systems that can wrap these engines for autonomous design closure -- not demos, not prototypes, but fully-specified engines with deterministic verification and pluggable EDA tool integration.

---

## DEVICE AUTO-PLACEMENT & ROUTING -- 7+ Years as Lead Architect

### DRC-Aware Routing Grids System (Patented -- US 11,790,147)

Central architect of Cadence's **Grid-Based Device Router (GBR)** -- a PDK-independent routing system for Virtuoso that separates DRC rule encoding from routing algorithm logic:

- **Routing Graph Architecture**: Designed a dynamic graph that is a virtual representation of all available routing space across all metal layers. Edges are classified as free, occupied, or haloed. Graph updates in real-time as routing segments and vias are created. Width-Spacing Pattern (WSP) tracks form the foundation; pre-routed shapes and route segments become occupied edges; DRC influence zones become halo edges.
- **Segment Service Module**: Computes shadows/halos for routed segments from cached DRC constraints (minArea, minLength, minSpacing, EOL, minSpanLengthSpacing). Returns shadow boxes for short segments and separate end-of-segment shadows for segments exceeding minimum length constraints.
- **Via Service Module**: Provides best via selection for each layer/width pair and computes clearance shadows/halos (same-mask, diff-mask, same-net, diff-net) that guarantee DRC correctness with other potential shapes.
- **Interlayer Edge System**: Intersections between different-layer tracks are treated as potential via locations with free/occupied status tracking.
- **PDK Enablement Results**: TSMC N7 (3 months), N5 (2 months), N3 (1 month), Cadence GPDK (2 weeks) -- each successive node faster due to architecture scalability. The GBR abstracts all DRC rules into the graph, keeping the routing engine PDK-independent. DRC/LVS signoff compliance is verified incrementally as each segment and via is placed.
- **Multi-Net Resource Allocation**: Addressed the fundamental congestion problem where greedy net-sequential routing causes early-routed nets to occupy all lower-metal resources, blocking subsequent nets.

### Bayesian Optimization of Router Parameters (Internal R&D)

Led exploration of **Optuna-based hyperparameter optimization** (Python) applied to the GBR, treating the router as a black box:

- **Framework**: Virtuoso Studio calls Python Flask REST API, which invokes Python/Optuna. Optuna suggests trial routing parameters (preferred layer settings, via/metal costs, net ordering); routed models are evaluated and scores fed back for Bayesian incorporation.
- **Congestion Reduction**: Optimizer discovers parameters that dissuade the router from greedily occupying lower-metal layers, instead encouraging topologies that escape to higher, vertical layers -- reducing resource contention for multi-net routing.
- **Resistance Optimization**: Optimizer steers routing toward less-resistive higher metal layers instead of highly-resistive lower metals (M0), resulting in 57% reduction in highly-resistive metal usage and 352-627% increase in moderately/less resistive metal usage.
- **Results**: Reduced opens from 25 to 3 on representative testcases (score from 28M to 3M). Validated on TSMC N3 (M0 on/off), Intel 1278. 100 trials on 20 parallel threads, optimal parameters at trial 60-80. Can run in 5x single-route duration.
- **Proposed Solutions**: Warm-start (reuse pre-trained parameters across similar PDK/designs without re-training) and one-click autonomous routing (allocate training duration, walk away, get optimal routing).

### Device Fill, Placement & Infrastructure

- **Metal & Poly Filler Engines**: Architected Virtuoso's density fill engines -- antenna fixing (jumpers, diodes), layer density analysis, DFM compliance for advanced nodes.
- **SADP Grid Infrastructure**: Grid models, checkers, and tools for Self-Aligned Double Patterning at 10nm/7nm -- enabling placement and routing readiness.
- **Width-Spacing Patterns** (US 10,733,351): Automatic WSP generation for color-based multi-patterning track systems by examining instance heights, placement orientations, pin widths/spacing/colors, and handling flipped/mirrored instances.
- **Automatic Placement/Routing Infrastructure Generation**: Developed infrastructure for automatic device placement and routing on advanced nodes.

---

## AGENTIC AI: Autonomous Memory Chip Assembly Routing Engine

Developed a **production-scale agentic framework** that automates complex VLSI routing orchestration for memory chip assembly (DRAM/SRAM/Flash) -- replacing manual design closure with agent-driven, mathematically verified automation.

### Architecture (10,000+ lines of specification-complete design documents)

| Component | Description | Scale |
|:---|:---|:---|
| **Memory Routing Architecture v4.8** | Domain architecture for memory chip assembly routing -- Block (array internals: bitlines, wordlines, sense amps) vs Periphery (fishbone topology: spine, ribs, tap connections). Two distinct routing domains sharing algorithms and verification. | 2,155 lines |
| **C++ Implementation Spec v2.0** | `RouterBase` -> `BlockRouter` + `PeripheryRouter` hierarchy. `RoutingToolPool` with 14 stateful function objects. EDA callbacks for Virtuoso, Innovus, IC Compiler II, OpenROAD. Database adapters for OpenAccess, Catena, CSV. | 2,261 lines |
| **Pin-to-Spine Tool (P2S) v1.0** | Standalone `libp2s.a` static library + CLI binary. 39 individually-callable function objects in 7-phase pipeline. Bidirectional track model (mature node). Zero-shorts hard constraint. `extern "C"` API. CSV/OpenAccess/Catena adapters. | 1,650 lines |
| **Pin-to-Spine QoR Deep Dive** | Failure mode analysis: 12 open modes, 8 short modes, 14 DRC violation categories. 39-feature evaluation rubric (PA-1 through RD-6). 13 identified gaps in Virtuoso's existing implementation. 9 established industry techniques documented. | 999 lines |
| **LangGraph Orchestrator v3.0** | AI brain -- topology selection (H-tree vs Fishbone based on die aspect ratio), solver dispatch, DRC/LVS verification. Three-layer stack: Orchestrator + Solver Engine + Domain Workers. Six routing domain taxonomy. | 940 lines |
| **Meta-Optimizer (AMOF)** | LLM-guided multi-objective optimization framework. 5 solver pool (Gurobi, SciPy, Optuna, DEAP, OR-Tools). Completeness gate ensures no missing constraints before solving. GA chromosome design for routing. MCP server interface. | 1,865 lines |
| **VLSI Routing Topologies** | H-tree vs Fishbone routing analysis. Die geometry impact on clock/power/signal distribution. Transition zones. Hybrid topologies. DRAM internals: destructive read, charge sharing, folded bitline, sense amp drain, memory banking. | 1,928 lines |

### Key Design Decisions

- **Hybrid Architecture**: Python/LangGraph orchestrates strategy (algorithm selection, net ordering, solver switching); C++ engine executes at microsecond-per-edge speed. Built-in DRC/LVS verification at every routing step. Zero hallucination in the compute path.
- **Zero-Shorts Hard Constraint**: If a connection cannot be made without creating a short, leave it OPEN. An open is a known failure. A short is silent corruption.
- **Database Agnostic**: Pluggable adapter interface -- CSV (testing), OpenAccess (Cadence), Catena (IBM). Core engine has no external runtime dependency.
- **Capacity**: 1,000,000 nets targeting Samsung / SK Hynix / Micron memory scale across all die aspect ratios.

---

## PROFESSIONAL EXPERIENCE

### Cadence Design Systems — 21 years 6 months (October 2004 - Present)

#### Senior Software Architect (July 2024 - Present) | San Francisco Bay Area

- **Agentic AI + Device Routing**: Architecting the integration of Python-based frameworks (LangGraph, Crew AI, MCP Servers, AutoGen 2, OpenAI Agents SDK) with production device routing and fill tools for autonomous design closure.
- **Autonomous Memory Routing Engine**: Designed a C++ memory routing engine with LangGraph orchestration targeting advanced nodes (TSMC N2, Intel 18A) for memory chip assembly. Produced 10,000+ lines of architecture specifications across 7 interconnected documents.
- **Pin-to-Spine Tool**: Specified a standalone routing library (`libp2s.a`) with 39 function objects, 7-phase pipeline, bidirectional track model, and zero-shorts hard constraint.
- **Optimization Frameworks**: Implemented 20+ Python-based optimization solutions using Gurobi, SciPy, PuLP, OR-Tools, DEAP (Genetic Algorithms), and Optuna for routing, placement, and resource allocation problems.

#### Software Architect (August 2023 - July 2024) | San Jose, CA

- Continued evolution of the DRC-Aware Routing Grids System and density filling solutions for advanced process nodes.
- Extended device routing architecture for multi-foundry support across TSMC, Intel, and Samsung PDKs.

#### Software Architect (July 2022 - August 2023) | Montreal, Quebec, Canada

- Led Bayesian Optimization exploration for router parameter tuning -- Python/Optuna-based hyperparameter optimization treating the GBR as a black box, achieving 57% reduction in resistive metal usage and reducing opens from 25 to 3.

#### Software Architect (October 2020 - July 2022) | San Francisco Bay Area

- **Led 5-engineer team** architecting the **DRC-Aware Routing Grids System** (US 11,790,147 -- granted Nov 2023) -- a novel routing graph architecture that decouples PDK-specific DRC rules from core routing algorithms.
- **Routing Graph Design**: Dynamic graph representing all available routing space across all metal layers. Edges encode DRC rules via Segment Service and Via Service modules. Graph updates in real-time as segments and vias are created.
- **PDK Enablement Acceleration**: TSMC N7 (3 months), N5 (2 months), N3 (1 month), GPDK (2 weeks) -- each successive node faster due to architecture scalability.
- Developed density filling and automatic placement/routing infrastructure generation solutions for 5nm and 3nm nodes.
- Designed modular C++ and SKILL-based architecture for high performance with fast-changing requirements.

#### Senior Member of Consulting Staff / R&D Engineering Manager (November 2019 - October 2020) | San Jose

- Managed team of 5 engineers developing a fast device routing solution.
- Direct technical oversight of routing engine performance, quality, and advanced node enablement.

#### Senior Member of Consulting Staff -- Individual Contributor (February 2016 - November 2019) | San Jose

- **Metal & Poly Filler Engines**: Primary developer for Virtuoso's metal and poly layer filler engines, including antenna fixing modules (jumpers, diodes) and layer density analysis -- critical infrastructure for device routing quality.
- **SADP Grid Infrastructure**: Developed grid models, checkers, and helper tools for Self-Aligned Double Patterning processes -- enabling layout readiness for placement and routing at 10nm and 7nm nodes.
- **Width-Spacing Patterns** (US 10,733,351): Automatic WSP generation for color-based multi-patterning track systems -- the foundation upon which the routing graph is built.
- **Technology Stack**: C++, STL, Boost, Qt-based infrastructure.

#### Senior Member of Consulting Staff (July 2013 - February 2016) | Noida, India

Led development of major features in **Virtuoso Layout Editor** (>70% market share, industry-standard custom layout tool):

- **FinFET Support**: Implemented advanced node (16nm and below) editing capabilities for finFET devices.
- **Neighborhood Awareness**: Designed and built context-sensitive editing commands with aperture-based layout analysis (patented).
- **Motif-to-Qt Migration**: Led the transition of the legacy Motif-based GUI to Qt framework.
- **New Features**: Layer Palette, Display Resource Editor, Fast Alignment Command, Smart Ruler, Fluid Guard-ring Support.
- **Awards**: Won 2 prestigious organizational awards. 3 patents granted (US 9305133, US 8612923, US 8271909).

#### Member of Consulting Staff (July 2010 - July 2013) | Noida, India

- **Constraint GUI**: Designed and built from scratch a Qt-based constraint editor supporting wide-range GUI operations for design and foundry constraints on selected objects. Won **Organization Level Award for Execution**.
- **Patent Granted**: 1 patent for constraint editing system.
- **Conference Paper**: Authored and presented paper at Cadence Technical Conference (US).
- Transitioned to Virtuoso Layout Editor — worked across almost all sub-tools.

#### Senior Member Technical Staff (April 2007 - July 2010) | Noida, India

- **Translators**: Primary developer for LEF-DEF and GDS/Stream translators — critical tools for design format interchange.
- **Performance & Quality**: Focused on coverage, Purify, and runtime performance optimization.
- **Awards**: Won 2 departmental awards.
- **Conference Paper**: Authored and presented paper at Cadence Technical Conference (US).

#### Member Technical Staff (October 2004 - April 2007) | Noida, India

- Early career development in EDA infrastructure and tool development.

---

### Mentor Graphics — Associate Member Technical Staff (October 2003 - October 2004) | Hyderabad, India

- **Primary lead** for porting DxDesigner front-end to Linux using Mainwin (Mainsoft) — providing MFC, COM runtime, and Windows registry equivalents on Unix.
- Served as primary contact for all Mainwin-related communications with Mainsoft engineering support across multiple Unix platforms.

---

### Geometric Software — Software Engineer (August 2002 - October 2003) | Pune, India

- Worked on CATIA CAD designer tool and ENOVIA PLM solutions from Dassault Systems.

---

## AGENTIC AI PROJECT PORTFOLIO

### EDA / Silicon Projects

| # | Project | Framework | Description |
|:---:|:---|:---|:---|
| 1 | **VLSI Memory Router** | LangGraph + C++ | Autonomous routing engine for DRAM/SRAM/Flash -- 7 interconnected spec documents, 10,000+ lines |
| 2 | **Pin-to-Spine Tool (P2S)** | C++, extern "C" | Standalone `libp2s.a` with 39 function objects, zero-shorts constraint, bidirectional tracks |
| 3 | **VLSI Routing (DEAP + A*)** | DEAP (GA), rustworkx | Multi-layer VLSI grid routing with genetic algorithm net ordering |
| 4 | **ModGen Scaling (RSMT)** | NetworkX, Steiner trees | Rectilinear Steiner minimum tree for module generation |

### Other Agentic AI Projects

| # | Project | Framework | Description |
|:---:|:---|:---|:---|
| 5 | **Meta-Optimizer (AMOF)** | LangGraph | LLM-guided solver selection with Gurobi/SciPy/DEAP/OR-Tools/Optuna pool and completeness gate |
| 6 | **Chat Assistant** | OpenAI Agents SDK | Multi-provider LLM (Gemini primary, GPT-4 fallback) orchestrating 14 MCP servers with rate-limiting and retry |
| 7 | **MedExplainer** | LangGraph | Medication journey narrator with 7 medical API sources, lab OCR, prescription OCR. 239 tests passing. |
| 8 | **College & Career Planner** | LangGraph, 6 MCP servers | AI-resilience scoring, RIASEC profiling, 7 specialized agents for student guidance |
| 9 | **Stock Picker** | Crew AI | Multi-agent stock analysis with sector filtering, S&P 500 metadata, yfinance integration |
| 10 | **Portfolio Manager** | Crew AI | Automated portfolio analysis and rebalancing recommendations |
| 11 | **Web Scraper** | AutoGen 2, MCP Fetch | Smart input parsing with optional recursive crawl and content extraction |

### MCP Servers (Custom Built)

| Server | Capability |
|:---|:---|
| **E*TRADE MCP** | Full brokerage integration -- accounts, portfolios, quotes, option chains, order placement via OAuth2 |
| **Gmail MCP** | Read, search, send emails via Gmail API with OAuth2 |
| **Outlook MCP** | Microsoft Outlook desktop integration for work email |
| **Pushover MCP** | Mobile push notifications with emergency escalation |
| **MCP Proxy** | Security sandbox -- intercepts and validates MCP tool calls between LLM and backend servers |

### Optimization & Algorithm Implementations

| Implementation | Solver/Library | Domain |
|:---|:---|:---|
| Facility Location Optimization | Gurobi | Capacitated facility placement |
| Multi-Objective Optimization | Gurobi | Weighted-sum scalarization with Pareto frontier |
| Electric Car Profit Maximization | SciPy | Nonlinear constrained optimization |
| N-Queens (3 approaches) | PuLP, OR-Tools, DEAP | LP, constraint programming, genetic algorithm comparison |
| Resource Assignment | PuLP | Integer programming for team allocation |
| Network Minimum-Cost Flow | NetworkX | Graph-based transportation optimization |
| Police Scheduling | PuLP | Shift scheduling with fairness constraints |
| Symbolic Regression | DEAP (GP) | Genetic programming for equation discovery |
| Travel Planning | OR-Tools | Vehicle routing with time windows |

---

## TECHNICAL PROFICIENCIES

| Domain | Technologies |
|:---|:---|
| **Device Routing** | Grid-Based Router (GBR), DRC-aware routing graphs, net ordering, congestion management, Segment/Via Services, PDK enablement |
| **Agentic AI Frameworks** | LangGraph, Crew AI, AutoGen 2, OpenAI Agents SDK, MCP (Model Context Protocol) Architecture |
| **LLM Integration** | Google Gemini (2.5 Flash/Pro), GPT-4.1, Claude (Code/Sonnet), multi-provider fallback chains |
| **EDA / Physical Design** | Virtuoso Layout Editor, Auto P&R, SKILL, OpenAccess API, DFM, LEF/DEF/GDSII translators |
| **Process Technology** | finFET, gaaFET PDKs -- TSMC (A14/16, N2/N2P/N3/N4/N5/N6/N7/N12/N16), Intel (18A/20A), Samsung (SF2/3/4/8) |
| **Optimization / Solvers** | Gurobi, SciPy, PuLP, OR-Tools, DEAP (Genetic Algorithms), Optuna (Bayesian) |
| **Graph Algorithms** | A*, Steiner Tree (RSMT), MST, KD-Tree, Network Flow, Two-Level GA, Pattern Router |
| **Languages** | C++, Python 3.x, Cadence SKILL, SQL |
| **C++ Infrastructure** | STL, Boost, Qt, CMake, pybind11, GoogleTest |
| **DevOps / Tools** | Git, Docker, CI/CD, VS Code (Agentic modes), Cursor, Claude Code |

---

## PATENTS (7 Granted)

1. **System and method for routing in an electronic design** (US 11,790,147 -- Nov 2023) — Dynamic routing graph generation from design rules, representing all available routing space across all layers, with real-time graph updates as routing segments and vias are created.
2. **Method, system, and computer program product for rearrangement of objects within an electronic design** (US 10,769,346 -- Sep 2020) — Automated layout object rearrangement: dragged objects trigger automatic repositioning of existing objects to resolve spacing violations and overlaps.
3. **Generating width spacing patterns for multiple patterning processes** (US 10,733,351 -- Aug 2020) — Automatic creation of width-spacing patterns (WSPs) for color-based track systems by examining instance heights, placement orientations, pin widths/spacing/colors, and handling flipped/mirrored instances.
4. **System and method for selective application and reconciliation of hierarchical ordered sets of circuit design constraints within a circuit design editor** (US 9,305,133 -- Apr 2016) — Semi-transparent constraint editor for in-context violation reconciliation during layout editing, with simplified hierarchy visualization and lookup-order modification.
5. **Methods, Systems, and Computer-Program Products for Item Selection and Positioning Suitable for High-Altitude and Context Sensitive Editing of Electrical Circuits** (US 8,612,923 -- Dec 2013) — High-altitude editing capabilities for congested layouts/schematics with context sensitivity, neighborhood awareness, and user-intent anticipation.
6. **System and method for process rules editors** (US 8,327,315 -- Dec 2012) — Graphical editor for visualizing, modifying, creating, and removing process rules (constraints) through a GUI, with constraint groups associated to specific circuit design objects.
7. **System and method for aperture based layout data analysis to achieve neighborhood awareness** (US 8,271,909 -- Sep 2012) — Input infrastructure that gathers information surrounding the cursor's locality, analyzes it in view of the issued command, and automatically determines suitable targets or next operations.

---

## AWARDS & RECOGNITION

| Award | Context |
|:---|:---|
| **Organization Level Award** (x2) | Virtuoso Layout Editor features and Constraint GUI |
| **Excellence in Innovation** | Novel approaches to layout automation |
| **Excellence in Execution** | Constraint GUI delivered from scratch |
| **Cadence Encore Award** | Outstanding technical contribution |
| **Departmental Awards** (x2) | LEF-DEF/GDS translator performance and quality |

---

## PUBLICATIONS

- Paper presented at **Cadence Technical Conference** (US) — Constraint editing system design and implementation.
- Paper presented at **Cadence Technical Conference** (US) — LEF-DEF/GDS translation performance optimization.

---

## EDUCATION

**Bachelor of Technology (B.Tech), Mechanical Engineering**
Indian Institute of Technology (IIT), Delhi — Class of 2002

---

## STRATEGIC CUSTOMER ENGAGEMENTS

Direct technical lead for critical EDA support and methodology deployments at:

**Tier 1 Silicon**: Apple, NVIDIA, Intel, Samsung, TSMC, Qualcomm
**Tier 2 Silicon**: Broadcom, AMD, Sony, MediaTek, Amazon, Altera, Microsoft, Infineon, Renesas

---

*References available upon request.*
