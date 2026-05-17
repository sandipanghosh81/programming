# AI Agent Memory Systems — Complete Guide

> A comprehensive reference covering memory types, implementations, and patterns  
> for LangGraph, CrewAI, and agentic AI systems.  
> Living document — updated as new concepts are explored.

---

## Table of Contents

1. [What is Memory in AI Agents?](#1-what-is-memory-in-ai-agents)
2. [Memory Types Overview](#2-memory-types-overview)
3. [Deep Dive: Each Memory Type](#3-deep-dive-each-memory-type)
   - [Short-Term Memory](#31-short-term-memory)
   - [Long-Term Memory](#32-long-term-memory)
   - [Entity Memory](#33-entity-memory)
   - [Semantic Memory (RAG)](#34-semantic-memory-rag)
   - [Episodic Memory](#35-episodic-memory)
   - [Procedural Memory](#36-procedural-memory)
   - [Checkpointing (LangGraph-specific)](#37-checkpointing-langgraph-specific)
4. [CrewAI vs LangGraph Memory Mapping](#4-crewai-vs-langgraph-memory-mapping)
5. [RAG Explained (Plain English)](#5-rag-explained-plain-english)
6. [Architecture Diagrams](#6-architecture-diagrams)
7. [Implementation Examples](#7-implementation-examples)
8. [When to Use What](#8-when-to-use-what)
9. [Glossary](#9-glossary)

---

## 1. What is Memory in AI Agents?

Memory in AI agents answers one fundamental question:

> **"What should the agent remember, and for how long?"**

Without memory, every interaction starts from zero — the agent has no idea who you are,
what you asked before, or what it learned. Memory makes agents useful across time.

```
┌─────────────────────────────────────────────────────┐
│                    AGENT MEMORY                      │
│                                                      │
│   "What's happening     "What happened    "What do   │
│    RIGHT NOW?"           BEFORE?"          I KNOW?"  │
│                                                      │
│   ┌──────────┐      ┌──────────────┐   ┌─────────┐  │
│   │Short-Term│      │  Long-Term   │   │Semantic │  │
│   │  Memory  │      │   Memory     │   │ Memory  │  │
│   │          │      │              │   │ (RAG)   │  │
│   │ Current  │      │ Past sessions│   │Knowledge│  │
│   │ session  │      │ User profiles│   │  base   │  │
│   └──────────┘      └──────────────┘   └─────────┘  │
│                                                      │
└─────────────────────────────────────────────────────┘
```

### Analogy: Human Memory

| Human Memory | Agent Equivalent | Example |
|---|---|---|
| Working memory (what you're thinking about right now) | Short-Term | "User just asked about metformin" |
| Autobiographical memory (your life experiences) | Episodic | "Last time I checked this drug combo, it was dangerous" |
| Factual memory (things you learned in school) | Semantic / RAG | "NSAIDs affect kidney function" |
| Muscle memory (how to ride a bike) | Procedural | "When checking interactions, normalize names first" |
| Recognizing people/places | Entity | "User: age 67, diabetic, allergic to penicillin" |

---

## 2. Memory Types Overview

| Memory Type | Plain English | Lifespan | Storage | Auto in CrewAI? | LangGraph Primitive |
|---|---|---|---|---|---|
| **Short-Term** | What's happening *right now* | Single session | RAM | ✅ Yes | `State` TypedDict |
| **Long-Term** | What agent learned from *past sessions* | Across sessions | SQLite/Postgres | ✅ Yes | `Store` |
| **Episodic** | Specific *past experiences* to recall | Across sessions | DB | ✅ Yes | Checkpointer + Store |
| **Semantic** | *General knowledge* (RAG) | Permanent | Vector DB | ✅ Yes | Vector store tool |
| **Procedural** | *How to do things* | Permanent | Code | Partial (role/backstory) | Graph structure |
| **Entity** | Facts about *specific things/people* | Across sessions | DB | ✅ Yes | Store namespaces |
| **Checkpointing** | Pause/resume/replay/time-travel | Across sessions | SQLite/Postgres | ❌ No | `Checkpointer` |

### Lifespan Visualization

```
Time ──────────────────────────────────────────────────────►

Session 1          Session 2          Session 3
┌──────────┐      ┌──────────┐      ┌──────────┐
│          │      │          │      │          │
│ Short-   │      │ Short-   │      │ Short-   │
│ Term     │      │ Term     │      │ Term     │
│ (dies)   │      │ (dies)   │      │ (dies)   │
└──────────┘      └──────────┘      └──────────┘

═══════════════════════════════════════════════════  Long-Term (persists forever)
═══════════════════════════════════════════════════  Entity (persists forever)
═══════════════════════════════════════════════════  Semantic/RAG (persists forever)
═══════════════════════════════════════════════════  Episodic (persists forever)

───────── ═════════════════════════════════════════  Checkpoints (from first save onward)
```

---

## 3. Deep Dive: Each Memory Type

### 3.1 Short-Term Memory

> **What's happening RIGHT NOW in this conversation.**

```
┌─────────────────────────────────────────┐
│           SHORT-TERM MEMORY             │
│                                         │
│  User: "I take metformin and aspirin"   │
│     │                                   │
│     ▼                                   │
│  State: {                               │
│    messages: [...],                      │
│    medications: ["metformin","aspirin"], │
│    interactions_found: [],              │
│    current_step: "normalizing"          │
│  }                                      │
│     │                                   │
│  Passed from node ──► to node ──► ...   │
│                                         │
│  ⚠️ GONE when session ends              │
└─────────────────────────────────────────┘
```

| Aspect | CrewAI | LangGraph |
|---|---|---|
| **What** | Auto-collected task outputs, agent observations | Graph `State` dict — you define the schema |
| **Scope** | Current crew execution | Current thread |
| **Implementation** | Automatic (`memory=True`) | You define `State` TypedDict |
| **Access** | Agents see relevant context auto-injected into prompts | Nodes read/write state explicitly |

**LangGraph Implementation:**
```python
from typing import TypedDict, Annotated
from langgraph.graph import add_messages

class State(TypedDict):
    messages: Annotated[list, add_messages]  # conversation history
    medications: list[str]                    # extracted from current query
    interactions_found: list[dict]            # results accumulated so far
    current_step: str                         # pipeline progress tracker
```

---

### 3.2 Long-Term Memory

> **What the agent remembers from PAST sessions — survives restarts.**

```
┌──────────────────────────────────────────────┐
│            LONG-TERM MEMORY                  │
│                                              │
│  Session 1 (Jan):                            │
│    "User asked about metformin + ibuprofen"  │
│    Result: "moderate interaction found"       │
│         │                                    │
│         ▼  (saved to DB)                     │
│                                              │
│  Session 2 (Feb):                            │
│    Agent recalls: "Last time we found a      │
│    moderate interaction with these drugs"     │
│                                              │
│  Storage: SQLite / Postgres / Redis          │
│  Survives: Process restarts, machine reboots │
└──────────────────────────────────────────────┘
```

| Aspect | CrewAI | LangGraph |
|---|---|---|
| **What** | Past task outcomes + learned patterns | Cross-thread `Store` |
| **Storage** | SQLite (automatic: `long_term_memory_storage.db`) | You choose: SQLite, Postgres, Redis |
| **Access** | Auto-retrieved when similar tasks run | You query the Store in your nodes |

**LangGraph Implementation:**
```python
from langgraph.store.memory import InMemoryStore
# Production: from langgraph.store.postgres import PostgresStore

store = InMemoryStore()

# Save (from any thread)
store.put(
    namespace=("user", "user_123", "sessions"),
    key="2026-02-16",
    value={"query": "metformin + ibuprofen", "result": "moderate interaction"}
)

# Retrieve (from any thread, any time)
item = store.get(("user", "user_123", "sessions"), "2026-02-16")
```

---

### 3.3 Entity Memory

> **Structured facts about specific people, drugs, companies, etc.**

```
┌─────────────────────────────────────────────────┐
│              ENTITY MEMORY                       │
│                                                  │
│  ┌─────────────────┐  ┌──────────────────────┐  │
│  │ Patient: John   │  │ Drug: Metformin      │  │
│  │ Age: 67         │  │ Class: Biguanide     │  │
│  │ Diabetic: Yes   │  │ Cleared by: Kidneys  │  │
│  │ Meds: 3         │  │ Watch: Renal fn      │  │
│  │ Allergies:      │  │ Interactions: 47     │  │
│  │   penicillin    │  │                      │  │
│  └─────────────────┘  └──────────────────────┘  │
│                                                  │
│  ┌─────────────────┐  ┌──────────────────────┐  │
│  │ Patient: Mary   │  │ Drug: Lisinopril     │  │
│  │ Age: 45         │  │ Class: ACE Inhibitor │  │
│  │ ...             │  │ ...                  │  │
│  └─────────────────┘  └──────────────────────┘  │
└─────────────────────────────────────────────────┘
```

| Aspect | CrewAI | LangGraph |
|---|---|---|
| **What** | Auto-extracts entities from conversations via LLM | You build extraction logic |
| **How** | LLM-powered (automatic with `memory=True`) | Store namespaces per entity type |
| **Best for** | Quick prototypes | Production systems needing precise control |

**LangGraph Implementation:**
```python
# Each entity type gets its own namespace
store.put(("patient", "john_doe"), "profile", {
    "age": 67,
    "conditions": ["type 2 diabetes", "hypertension"],
    "medications": ["metformin 500mg", "lisinopril 10mg"],
    "allergies": ["penicillin"],
})

store.put(("drug", "metformin"), "known_interactions", {
    "nsaids": {"severity": "moderate", "mechanism": "renal clearance reduction"},
    "alcohol": {"severity": "major", "mechanism": "lactic acidosis risk"},
})
```

---

### 3.4 Semantic Memory (RAG)

> **General knowledge the agent can search — like a reference library.**

This is the **RAG** (Retrieval-Augmented Generation) pattern. See [Section 5](#5-rag-explained-plain-english) for full explanation.

```
┌──────────────────────────────────────────────────────┐
│              SEMANTIC MEMORY (RAG)                    │
│                                                      │
│  Step 1: SETUP (one-time)                            │
│                                                      │
│  FDA Drug Labels ──► Chunk into paragraphs           │
│  Research Papers      │                              │
│  Medical Textbooks    ▼                              │
│                   Embedding Model                    │
│                   (text → numbers)                   │
│                       │                              │
│                       ▼                              │
│                  ┌──────────┐                        │
│                  │ Vector   │  Each chunk stored     │
│                  │ Database │  as a list of ~1,536   │
│                  │ (Chroma) │  numbers representing  │
│                  └──────────┘  its MEANING           │
│                                                      │
│  Step 2: QUERY (at runtime)                          │
│                                                      │
│  "metformin kidney risk"                             │
│        │                                             │
│        ▼                                             │
│  Embedding Model (same one)                          │
│        │                                             │
│        ▼                                             │
│  Search vector DB for nearest matches                │
│        │                                             │
│        ▼                                             │
│  Top 5 most relevant chunks returned                 │
│        │                                             │
│        ▼                                             │
│  Fed to LLM as context → LLM generates answer       │
└──────────────────────────────────────────────────────┘
```

**Key Insight — Vector DB vs SQL:**

```
SQL:     SELECT * FROM drugs WHERE name = 'metformin'
         ▲ Exact match only. Must know column names.

Vector:  search("elderly diabetic kidney problems with sugar medication")
         ▲ Matches by MEANING. "Renal impairment in geriatric patients
           on hypoglycemic agents" would match — even with ZERO shared words.
```

**How text becomes searchable numbers (embeddings):**

```
"Metformin is cleared by the kidneys"
        │
        ▼  (embedding model — small, free, runs locally)

[0.023, -0.841, 0.112, 0.567, ... 1,536 numbers total]

Similar sentences → similar number patterns → that's how meaning-search works
```

---

### 3.5 Episodic Memory

> **Specific past experiences the agent can recall and learn from.**

```
┌──────────────────────────────────────────────────┐
│              EPISODIC MEMORY                      │
│                                                   │
│  Episode 1 (Jan 15):                              │
│  ┌──────────────────────────────────────────┐     │
│  │ Query: "metformin + ibuprofen"           │     │
│  │ Steps taken: RxNorm → OpenFDA → PubMed   │     │
│  │ Result: Moderate interaction found        │     │
│  │ User feedback: "Very helpful, thanks!"    │     │
│  └──────────────────────────────────────────┘     │
│                                                   │
│  Episode 2 (Jan 22):                              │
│  ┌──────────────────────────────────────────┐     │
│  │ Query: "can I add aspirin to my regimen?" │     │
│  │ Steps taken: RxNorm → OpenFDA → PubMed   │     │
│  │ Result: Low-dose aspirin OK with caution  │     │
│  │ User feedback: "Confirmed with my doctor" │     │
│  └──────────────────────────────────────────┘     │
│                                                   │
│  Agent uses past episodes to:                     │
│  - Avoid repeating work                           │
│  - Improve answers based on feedback              │
│  - Reference prior findings                       │
└──────────────────────────────────────────────────┘
```

| Aspect | CrewAI | LangGraph |
|---|---|---|
| **What** | Auto-stored from long-term memory | Checkpointer snapshots + Store queries |
| **Implementation** | Built into `memory=True` | Combine checkpointer (state history) with Store (outcomes) |

---

### 3.6 Procedural Memory

> **How to do things — the agent's learned workflows and skills.**

```
┌─────────────────────────────────────────────────┐
│            PROCEDURAL MEMORY                     │
│                                                  │
│  "When checking drug interactions, ALWAYS:"      │
│                                                  │
│   1. Normalize drug names (RxNorm)               │
│   2. Check pairwise interactions (OpenFDA)       │
│   3. Look up individual side effects             │
│   4. Search for recent research (PubMed)         │
│   5. Synthesize into plain English report        │
│                                                  │
│  In CrewAI: Encoded in agent role + backstory    │
│  In LangGraph: Encoded in graph structure        │
│                (nodes + edges = the procedure)   │
└─────────────────────────────────────────────────┘
```

| Aspect | CrewAI | LangGraph |
|---|---|---|
| **What** | Agent role, backstory, tool descriptions | The graph itself (nodes + edges) |
| **Implementation** | YAML/Python agent definitions | `StateGraph` with `add_node()` / `add_edge()` |
| **Modifiable at runtime?** | Limited | Yes — conditional edges act as decision-making |

---

### 3.7 Checkpointing (LangGraph-Specific)

> **Save the agent's complete state at every step — enables time-travel, pause/resume, and crash recovery.**

CrewAI does NOT have an equivalent of this.

```
┌────────────────────────────────────────────────────────┐
│              CHECKPOINTING                              │
│                                                        │
│  Node A ──────► Node B ──────► Node C ──────► Node D   │
│    │              │              │              │       │
│    ▼              ▼              ▼              ▼       │
│  ┌────┐        ┌────┐        ┌────┐        ┌────┐     │
│  │ CP │        │ CP │        │ CP │        │ CP │     │
│  │ #1 │        │ #2 │        │ #3 │        │ #4 │     │
│  └────┘        └────┘        └────┘        └────┘     │
│                                                        │
│  Saved to: SQLite / Postgres                           │
│                                                        │
│  Enables:                                              │
│  ├─ ⏸️  Pause at Node B, wait for human approval       │
│  ├─ ▶️  Resume from Node B after approval              │
│  ├─ ⏪ Replay from Node A with different inputs        │
│  ├─ 🔍 Debug: inspect exact state at any step          │
│  └─ 💥 Crash recovery: restart from last checkpoint    │
└────────────────────────────────────────────────────────┘
```

**LangGraph Implementation:**
```python
from langgraph.checkpoint.sqlite import SqliteSaver

checkpointer = SqliteSaver.from_conn_string("checkpoints.db")

graph = builder.compile(
    checkpointer=checkpointer,  # every state transition is saved
    store=store,                 # cross-thread long-term memory
)

# Run with a thread_id (each thread = separate conversation)
config = {"configurable": {"thread_id": "user_123_session_1"}}
result = graph.invoke({"messages": [user_input]}, config)

# Later: resume, replay, or inspect any checkpoint
```

---

## 4. CrewAI vs LangGraph Memory Mapping

### Side-by-Side Comparison

```
┌───────────────────────────────────────────────────────────┐
│                                                           │
│   CrewAI                          LangGraph               │
│   ══════                          ═════════               │
│                                                           │
│   crew = Crew(                    State (TypedDict)       │
│     memory=True  ◄──────────────► + Checkpointer         │
│   )                               + Store                │
│                                                           │
│   Short-Term ◄────────────────── State dict               │
│   (auto)                         (you define schema)      │
│                                                           │
│   Long-Term ◄─────────────────── Store                    │
│   (SQLite auto)                  (you choose backend)     │
│                                                           │
│   Entity ◄────────────────────── Store namespaces         │
│   (LLM auto-extract)            (you build extraction)   │
│                                                           │
│   Contextual ◄────────────────── State + message history  │
│   (auto-assembled)               (you assemble in nodes)  │
│                                                           │
│   ❌ No equivalent ◄──────────── Checkpointer             │
│                                  (time-travel, resume)    │
│                                                           │
│   Approach: "Batteries included"  "Build what you need"   │
│   Control:  Low                   High                    │
│   Speed:    Fast to start         More code, more power   │
│                                                           │
└───────────────────────────────────────────────────────────┘
```

### Detailed Feature Matrix

| Memory Type | Goal | CrewAI | LangGraph | Storage Backend |
|---|---|---|---|---|
| **Short-Term** | Current conversation context | `memory=True` (auto) | `State` TypedDict | RAM |
| **Long-Term** | Remember across sessions | SQLite (auto) | `Store` (you configure) | SQLite/Postgres/Redis |
| **Entity** | Facts about specific things | Auto-extracted by LLM | `Store` namespaces (you build) | Same as long-term |
| **Semantic/RAG** | Searchable knowledge base | Built-in embeddings | Vector store as tool | ChromaDB/FAISS/Pinecone |
| **Episodic** | Past experiences to learn from | Long-term memory (auto) | Checkpointer + Store queries | SQLite/Postgres |
| **Procedural** | How to do things | Agent role/backstory + tools | Graph structure (nodes/edges) | Code |
| **Checkpointing** | Pause/resume/replay | ❌ Not available | `Checkpointer` (built-in) | SQLite/Postgres |
| **Contextual** | Assembled context per turn | Auto from short+entity | You build in node logic | RAM |

---

## 5. RAG Explained (Plain English)

### What is RAG?

**RAG = Retrieval-Augmented Generation**

> Before the LLM answers, go **fetch relevant information** first, then give it to the LLM as context.

### Analogy

- **Without RAG**: You ask someone a question, they answer from memory only. Might be wrong or outdated.
- **With RAG**: You ask someone a question, they first **look it up in a reference book**, then answer using what they found. Much more accurate.

### Step-by-Step Flow

```
1. User asks: "Does metformin interact with ibuprofen?"
                    │
2. RETRIEVAL:       │  Search your knowledge store for
                    │  relevant documents/chunks
                    ▼
3. Found 5 relevant text chunks:
   - "Metformin combined with NSAIDs may reduce kidney function..."
   - "Ibuprofen is an NSAID that can affect renal clearance..."
   - (etc.)
                    │
4. AUGMENTED:       │  Stuff these chunks into the LLM prompt
                    ▼
5. LLM prompt becomes:
   "Given the following medical information:
    [chunk 1] [chunk 2] [chunk 3]...

    Answer the user's question: Does metformin interact with ibuprofen?"
                    │
6. GENERATION:      │  LLM generates answer grounded in retrieved facts
                    ▼
7. "Yes — ibuprofen is an NSAID that can reduce kidney function.
    Since metformin is cleared by the kidneys, this combination
    may cause metformin to build up to dangerous levels..."
```

### When You Do vs Don't Need RAG

| Situation | RAG Needed? | Why |
|---|---|---|
| Data available via live API (OpenFDA, PubMed) | ❌ No | API call IS your retrieval |
| Large static documents (FDA drug labels, textbooks) | ✅ Yes | Too big to fit in LLM context |
| Structured database (SQL tables) | ❌ No | Use SQL queries instead |
| Free-text knowledge base (research papers) | ✅ Yes | Need meaning-based search |

---

## 6. Architecture Diagrams

### Memory Flow in a LangGraph Agent

```
                        ┌─────────────────┐
                        │   User Input     │
                        └────────┬────────┘
                                 │
                                 ▼
┌─────────────────────────────────────────────────────────┐
│                    LANGGRAPH AGENT                       │
│                                                         │
│  ┌──────────┐    ┌──────────┐    ┌──────────┐          │
│  │  Node A  │───►│  Node B  │───►│  Node C  │          │
│  └──────────┘    └──────────┘    └──────────┘          │
│       │               │               │                 │
│       │          ┌────┴────┐          │                 │
│       │          │  Tools  │          │                 │
│       │          │ RxNorm  │          │                 │
│       │          │ OpenFDA │          │                 │
│       │          │ PubMed  │          │                 │
│       │          └─────────┘          │                 │
│       │                               │                 │
│  ┌────┴───────────────────────────────┴────┐            │
│  │         STATE (Short-Term Memory)        │            │
│  │  messages, medications, results, step    │            │
│  └──────────────────┬──────────────────────┘            │
│                     │                                    │
└─────────────────────┼────────────────────────────────────┘
                      │
         ┌────────────┼────────────┐
         │            │            │
         ▼            ▼            ▼
   ┌──────────┐ ┌──────────┐ ┌──────────┐
   │Checkpoint│ │  Store   │ │Vector DB │
   │ (SQLite) │ │ (Long-   │ │ (RAG /   │
   │          │ │  Term +  │ │ Semantic)│
   │ Resume   │ │  Entity) │ │          │
   │ Replay   │ │          │ │ Search   │
   │ Debug    │ │ Profiles │ │ by       │
   │          │ │ History  │ │ meaning  │
   └──────────┘ └──────────┘ └──────────┘
    Persistent   Persistent   Persistent
```

### Memory Interaction Pattern

```
                    Query: "Add aspirin to my medications"
                                    │
                                    ▼
                    ┌───────────────────────────┐
                    │     1. SHORT-TERM          │
                    │     Load current state     │
                    │     (this conversation)    │
                    └─────────────┬─────────────┘
                                  │
                    ┌─────────────▼─────────────┐
                    │     2. ENTITY (Store)      │
                    │     Fetch user profile     │
                    │     Current meds list      │
                    └─────────────┬─────────────┘
                                  │
                    ┌─────────────▼─────────────┐
                    │     3. EPISODIC (Store)    │
                    │     Any past queries       │
                    │     about aspirin?         │
                    └─────────────┬─────────────┘
                                  │
                    ┌─────────────▼─────────────┐
                    │     4. SEMANTIC (RAG)      │
                    │     Search drug label DB   │
                    │     for aspirin info       │
                    └─────────────┬─────────────┘
                                  │
                    ┌─────────────▼─────────────┐
                    │     5. TOOLS (APIs)        │
                    │     RxNorm + OpenFDA       │
                    │     Live interaction check │
                    └─────────────┬─────────────┘
                                  │
                    ┌─────────────▼─────────────┐
                    │     6. LLM SYNTHESIS       │
                    │     Combine all memory     │
                    │     + tool results         │
                    │     → Plain English answer │
                    └─────────────┬─────────────┘
                                  │
                    ┌─────────────▼─────────────┐
                    │     7. CHECKPOINT          │
                    │     Save state snapshot    │
                    │     Update entity memory   │
                    │     Log episode            │
                    └───────────────────────────┘
```

---

## 7. Implementation Examples

### Minimal LangGraph with All Memory Types

```python
from typing import TypedDict, Annotated
from langgraph.graph import StateGraph, add_messages
from langgraph.checkpoint.sqlite import SqliteSaver
from langgraph.store.memory import InMemoryStore

# === SHORT-TERM: State schema ===
class State(TypedDict):
    messages: Annotated[list, add_messages]
    medications: list[str]
    interactions: list[dict]

# === LONG-TERM + ENTITY: Store ===
store = InMemoryStore()

# === CHECKPOINTING ===
checkpointer = SqliteSaver.from_conn_string("memory.db")

# === Build graph ===
builder = StateGraph(State)
builder.add_node("normalize", normalize_drugs)
builder.add_node("check_interactions", check_interactions)
builder.add_node("generate_report", generate_report)
builder.add_edge("normalize", "check_interactions")
builder.add_edge("check_interactions", "generate_report")

graph = builder.compile(
    checkpointer=checkpointer,
    store=store,
)

# === Run with thread (session) ID ===
config = {"configurable": {"thread_id": "user_123"}}
result = graph.invoke({"messages": [user_input]}, config)
```

### Accessing Store in a Node (Long-Term + Entity Memory)

```python
def check_interactions(state: State, *, store) -> dict:
    user_id = extract_user_id(state)
    
    # Read entity memory
    profile = store.get(("patient", user_id), "profile")
    
    # ... do interaction checking ...
    
    # Write to episodic memory
    store.put(
        ("patient", user_id, "episodes"),
        key=f"check_{datetime.now().isoformat()}",
        value={
            "medications_checked": state["medications"],
            "interactions_found": interactions,
        }
    )
    
    return {"interactions": interactions}
```

### CrewAI — All Memory in One Line

```python
from crewai import Crew

crew = Crew(
    agents=[normalizer, checker, reporter],
    tasks=[normalize_task, check_task, report_task],
    memory=True,       # ← enables ALL memory types
    verbose=True,
)

# Short-term: automatic during execution
# Long-term: saved to long_term_memory_storage.db
# Entity: auto-extracted from conversations
# Contextual: auto-assembled from short-term + entity
```

---

## 8. When to Use What

### Decision Tree

```
Do you need memory AT ALL?
│
├─ NO: Simple one-shot tool (e.g., unit converter)
│      → No memory needed
│
└─ YES: Agent needs context
   │
   ├─ Only within ONE conversation?
   │  → Short-Term Memory (State / messages)
   │
   ├─ Across MULTIPLE conversations?
   │  │
   │  ├─ Remember user preferences/profiles?
   │  │  → Entity Memory (Store namespaces)
   │  │
   │  ├─ Remember what happened last time?
   │  │  → Episodic Memory (Store + Checkpointer)
   │  │
   │  └─ Remember general knowledge?
   │     → Semantic Memory (RAG / Vector DB)
   │
   ├─ Need pause/resume/human-approval?
   │  → Checkpointing (LangGraph only)
   │
   └─ Need crash recovery?
      → Checkpointing with persistent backend (SQLite/Postgres)
```

### Project Phase Recommendations

| Phase | Memory Types | Complexity |
|---|---|---|
| **V1 / MVP** | Short-Term (State) + Checkpointer | Low — ~20 lines |
| **V2 / Users** | + Long-Term (Store) + Entity | Medium — +50 lines |
| **V3 / Knowledge** | + Semantic (RAG with vector DB) | Medium — +30 lines + setup |
| **V4 / Learning** | + Episodic (learn from past interactions) | Higher — custom logic |

---

## 9. Glossary

| Term | Definition |
|---|---|
| **Embedding** | Text converted to a list of numbers (~1,536) that represent its meaning. Similar text → similar numbers. |
| **Vector Database** | A database optimized for storing and searching embeddings. Finds "similar meaning" not "exact match". Examples: ChromaDB, FAISS, Pinecone. |
| **RAG** | Retrieval-Augmented Generation — fetch relevant docs before asking the LLM to answer. |
| **Checkpointer** | LangGraph component that saves complete state at every graph node. Enables resume, replay, debug. |
| **Store** | LangGraph component for persistent key-value storage across threads/sessions. Used for long-term and entity memory. |
| **Thread** | A conversation session in LangGraph, identified by `thread_id`. Each thread has its own state history. |
| **State** | The data structure passed between nodes in a LangGraph graph. Represents short-term memory. |
| **Namespace** | A hierarchical key path in LangGraph Store, e.g., `("patient", "john", "medications")`. Organizes entity memory. |
| **Chunk** | A piece of a larger document, sized to fit in LLM context. Typically 200–1,000 tokens. |
| **Context Window** | The maximum amount of text an LLM can process at once (e.g., 128K tokens for GPT-4o). |

---

*Last updated: February 16, 2026*  
*This is a living document — new memory concepts will be added as explored.*
