# The LangGraph Architectural Matrix — Comprehensive Reference

**Version baseline:** LangGraph `1.x` (stable, Oct 2025). Functional API (`@entrypoint`/`@task`) landed in 0.3.x.
**Philosophy:** LangGraph is a *low-level* runtime inspired by Google's Pregel & Apache Beam. It does NOT abstract prompts or agent architecture — it provides the execution substrate. You wire the logic; LangGraph handles state, persistence, streaming, and durable execution.
**Install:** `pip install langgraph langgraph-prebuilt langchain-openai` (LangChain models are optional — any chat model works.)

---

## 1. Core Graph Primitives

| Component | API | Owner / Decision Maker | Description & Key Mechanics |
|---|---|---|---|
| **StateGraph** | `StateGraph(StateSchema)` | Developer | The top-level graph builder. Parameterized by your state schema (TypedDict or Pydantic). Declares the full graph structure before compilation. Mutable at definition time; immutable once compiled. |
| **State** | `TypedDict` / Pydantic `BaseModel` | Developer (schema) → Nodes (updates) | The single source of truth shared across all nodes. Nodes receive the full state and return *partial updates* (a dict of changed keys only). Never mutate in-place — return a new dict. |
| **Reducers** | `Annotated[list, operator.add]` / `add_messages` | Developer declares, Runtime applies | Per-key merge strategy. Without a reducer, a key is **overwritten** by the last writer. With `operator.add`, values are *appended*. `add_messages` is a special reducer that appends messages but handles ID-based deduplication and updates. |
| **MessagesState** | `from langgraph.graph import MessagesState` | Prebuilt convenience | A ready-made `TypedDict` with a single `messages: Annotated[list[BaseMessage], add_messages]` field. Use as your base for any chat agent; subclass to add extra fields. |
| **Node** | `graph.add_node("name", fn)` | Developer registers, Graph calls | A Python function (sync or async) that takes `state` and returns a partial state dict. This is where computation happens — LLM calls, tool calls, business logic, routing decisions. |
| **START / END** | `from langgraph.graph import START, END` | Graph sentinels | `START` is the virtual entry point; `END` signals the graph is done. Edges *to* `END` terminate execution on that branch. |
| **compile()** | `graph.compile(checkpointer=..., store=..., interrupt_before=...)` | Developer | Locks the graph structure, validates all node/edge connections, and attaches the persistence layer. The returned `CompiledStateGraph` is what you actually run. |
| **ConfigSchema / ContextSchema** | `StateGraph(State, config_schema=MyConfig)` | Developer (defines), Caller (supplies at runtime) | Runtime configuration injected per-run — not stored in state. Use for things like user ID, model choice, feature flags. Access inside nodes via `config["configurable"]`. `context_schema` is the modern replacement for `config_schema` in v1+. |

---

## 2. Edge Types & Control Flow

| Edge Type | API | When to Use |
|---|---|---|
| **Normal Edge** | `graph.add_edge("a", "b")` | Always go from A to B — no conditions. |
| **Conditional Edge** | `graph.add_conditional_edges("a", router_fn, {"x": "node_x", "y": END})` | Router function inspects state and returns a string key. That key maps to the next node name. The canonical ReAct loop uses this to route `"tools"` vs `"end"`. |
| **Entry Point** | `graph.set_entry_point("node")` or `graph.add_edge(START, "node")` | Declares which node runs first. Both are equivalent. |
| **Send API** (Dynamic Fan-out) | `return [Send("worker_node", {"item": x}) for x in items]` | A router returns a *list* of `Send` objects instead of a string. Each `Send` spawns an independent execution of the named node with its own private state. Results are merged by reducer when the fan-in node runs. The correct pattern for parallel map-reduce. |
| **Command** (dynamic re-routing) | `return Command(goto="node_b", update={"key": val})` | A node returns a `Command` object instead of a plain dict. This simultaneously updates state AND overrides the next-node decision, enabling runtime re-routing that can't be expressed as a static conditional edge. Can also target a node in a *parent* graph with `Command(goto="parent_node", graph=Command.PARENT)`. |

---

## 3. Execution Modes

| Mode | API | Description |
|---|---|---|
| **Invoke** | `graph.invoke(input, config={"configurable": {"thread_id": "t1"}})` | Blocking. Runs the full graph to completion and returns the final state. |
| **Stream (values)** | `graph.stream(input, config, stream_mode="values")` | Yields the *complete state snapshot* after every node finishes. |
| **Stream (updates)** | `graph.stream(input, config, stream_mode="updates")` | Yields only the *partial update dict* each node returned. Lighter than "values". |
| **Stream (messages)** | `graph.stream(input, config, stream_mode="messages")` | Token-by-token streaming of LLM output. Yields `(AIMessageChunk, metadata)` tuples as tokens arrive. Essential for chatbot UX. |
| **Stream (custom)** | `ctx.write.dispatch({"key": val})` in node + `stream_mode="custom"` | Node pushes arbitrary data into the stream mid-execution. Useful for progress updates or intermediate results you want the UI to see before the node finishes. |
| **Stream (debug)** | `stream_mode="debug"` | Full execution trace — every checkpoint write, task start/end. Use for debugging only; verbose. |
| **Multiple modes** | `stream_mode=["messages", "updates"]` | Combine modes; each yielded item is `(mode_name, data)`. Common for chat UIs: stream tokens AND get node update events simultaneously. |
| **Async** | `await graph.ainvoke(...)` / `async for event in graph.astream(...)` | All methods have async counterparts. Prefer async in production API servers. |
| **subgraphs=True** | `graph.stream(..., subgraphs=True)` | Propagate stream events from nested subgraphs to the top-level stream. Required for interrupt detection across subgraph boundaries. |

---

## 4. Persistence — Checkpointers

The checkpointer is what turns a stateless function graph into a durable, resumable execution engine. Every node transition writes a checkpoint.

| Concept | API | Description |
|---|---|---|
| **Thread** | `config={"configurable": {"thread_id": "my-thread"}}` | A logical conversation / run instance. All checkpoints under the same `thread_id` form one durable history. Reuse the same ID to resume; use a new ID to start fresh. |
| **InMemorySaver** | `from langgraph.checkpoint.memory import InMemorySaver` | Dev/test only. Stores checkpoints in a dict. Lost on process restart. |
| **PostgresSaver** | `from langgraph.checkpoint.postgres import PostgresSaver` | Production. Stores in Postgres. Survives restarts. Also: `SqliteSaver`, `RedisSaver` from community packages. |
| **Checkpoint** | `graph.get_state(config)` → `StateSnapshot` | Returns the latest `StateSnapshot` for a thread: the state, the `next` nodes to execute, the `config`, and any pending `interrupts`. |
| **State History** | `graph.get_state_history(config)` | Returns all checkpoints for a thread, ordered newest-first. Each is a `StateSnapshot`. |
| **Time Travel** | `graph.invoke(None, config={"configurable": {"thread_id": "t1", "checkpoint_id": old_id}})` | Re-run the graph from any historical checkpoint. Enables "undo", A/B branching, and debugging by replaying from a known-good state. |
| **State Edit** | `graph.update_state(config, values={"key": new_val})` | Surgically overwrite specific state keys in the current checkpoint *without* re-running any nodes. The canonical way to let a human correct an LLM's output before resuming. Use `as_node="node_name"` to make the edit look like it came from a specific node (important for reducers). |

---

## 5. Human-in-the-Loop (HITL)

| Mechanism | API | When to Use |
|---|---|---|
| **Dynamic interrupt** | `interrupt("payload")` inside any node | Pause mid-execution from inside node logic. Can be conditional. Payload (any JSON-serializable value) is surfaced to the caller via `state["__interrupt__"]`. Resume by calling `graph.stream(Command(resume=value), config)`. The node *re-executes from the start* on resume; `interrupt()` returns the `resume` value the second time. |
| **Static breakpoint (before)** | `graph.compile(interrupt_before=["node_name"])` | Always pause *before* a specific node runs. Useful for blanket approval gates on destructive nodes. Resume with `graph.stream(None, config)`. |
| **Static breakpoint (after)** | `graph.compile(interrupt_after=["node_name"])` | Always pause *after* a node runs, before the edge fires. Good for "review output before continuing." |
| **State review & edit** | `graph.get_state(config)` + `graph.update_state(config, ...)` | Between interrupt and resume, inspect the full state, surgically fix values, then continue. This is the pattern for "human edits LLM draft before publication." |
| **Multiple interrupts** | `interrupt("q1"); interrupt("q2")` in same node | Multiple `interrupt()` calls in one node are resolved sequentially. Resume values are matched in order. Use `Command(resume={"interrupt_id": val})` to map resume values explicitly when parallel branches all interrupt simultaneously. |

---

## 6. Long-Term Memory — Stores

Separate from the checkpointer (which is thread-scoped). The Store is **cross-thread** — persistent memory that survives individual runs.

| Concept | API | Description |
|---|---|---|
| **BaseStore** | `from langgraph.store.base import BaseStore` | Interface for namespaced key-value storage. Used for user preferences, learned facts, cross-session context. |
| **InMemoryStore** | `from langgraph.store.memory import InMemoryStore` | Dev/test. Compile with `graph.compile(store=store)`. |
| **Accessing in a node** | `def node(state, store: Annotated[BaseStore, InjectedStore])` | Inject the store into a node via type annotation. LangGraph supplies it automatically at runtime. |
| **Namespace** | `store.put(("user", user_id), "prefs", {...})` / `store.get(("user", user_id), "prefs")` | Hierarchical tuple namespace. First arg is the namespace tuple, second is the key. Prevents cross-user data leaks. |
| **Search** | `store.search(("user", user_id), query="...")` | Semantic/fuzzy search across items in a namespace. Enables RAG-style memory retrieval without a separate vector DB for moderate scale. |

---

## 7. Streaming Output — ToolNode & InjectedState

| Component | API | Description |
|---|---|---|
| **ToolNode** | `from langgraph.prebuilt import ToolNode` | Prebuilt node that receives a state with `messages[-1].tool_calls`, executes the called tools in parallel (v1 mode) or via Send API (v2 mode), and appends `ToolMessage` results. Drop-in tool executor for ReAct graphs. |
| **InjectedState** | `def my_tool(x: str, state: Annotated[State, InjectedState]) -> str` | Inject the *current graph state* into a tool function without exposing it as an LLM-visible parameter. Use for tools that need context (e.g., "what was the last user message?") without polluting the tool schema. |
| **InjectedStore** | `def my_tool(x: str, store: Annotated[BaseStore, InjectedStore])` | Same injection pattern but for the cross-thread Store. Lets a tool read/write long-term memory while the LLM only sees the tool's declared parameters. |
| **InjectedToolCallId** | `tool_call_id: Annotated[str, InjectedToolCallId]` | Inject the tool call's unique ID for building `ToolMessage` responses manually. Needed when a tool returns multiple messages or needs to reference its own invocation. |

---

## 8. Multi-Agent Patterns

| Pattern | API | Decision Maker | Description |
|---|---|---|---|
| **Subgraph (as node)** | `parent.add_node("child", child_graph.compile())` | Parent graph | A compiled `StateGraph` used as a node inside another graph. The parent state is passed in; only the overlapping keys flow through. Full isolation — child has its own checkpointer namespace. Use `subgraphs=True` on `stream()` to see events from both. |
| **Supervisor** | `create_supervisor([agent_a, agent_b], model=llm, ...)` (from `langgraph-supervisor`) | Supervisor LLM | A central LLM decides which specialist agent to hand off to via tool calls. Agents are compiled `StateGraph`s (often `create_react_agent` instances). The supervisor uses `create_handoff_tool` internally. **Current recommendation:** implement the supervisor as a plain LLM-with-tools node for more context-engineering control, rather than using the library. |
| **Swarm** | `create_swarm(agents, default_active_agent=...)` (from `langgraph-swarm`) | Active agent + handoff tools | Peer-to-peer multi-agent. Each agent can hand off to any other via a handoff tool. No central coordinator. Agents share a message history across handoffs. |
| **Map-Reduce via Send** | Router returns `[Send("worker", {"item": x}) for x in items]` + reducer on result key | Developer-defined router | Fan-out N tasks in parallel; fan-in with a reducer. The cleanest pattern for "process each item independently then aggregate." |
| **Reflection / Critique** | Two-node loop: `generator → critic → (conditional back to generator OR end)` | Conditional edge router | Generator produces output; critic evaluates; loop continues until quality threshold or max iterations. Pure graph topology, no special API. |

---

## 9. Prebuilt Agents

| Component | API | When to Use |
|---|---|---|
| **create_react_agent** | `from langgraph.prebuilt import create_react_agent` | The fast path. Produces a fully-wired ReAct graph (LLM node → conditional edge → ToolNode → back to LLM). Accepts `model`, `tools`, `prompt`, `checkpointer`, `store`. Returns a `CompiledStateGraph`. **Use this unless** you need parallel node execution, custom retry logic, complex multi-step branching, or non-standard state shapes. |
| **Tool call version** | `create_react_agent(..., tool_call_version="v2")` | v2 distributes tool calls across parallel nodes via Send API instead of processing them serially in ToolNode. Better throughput for multiple tool calls in one LLM turn. |
| **Dynamic model selection** | `model=lambda state, runtime: ChatOpenAI(model=state.get("model", "gpt-4"))` | Pass a callable instead of a model instance. Runtime selects which model to call based on state or context. Enables per-user model routing in multi-tenant apps. |

---

## 10. Functional API (`@entrypoint` / `@task`)

A newer, Python-native alternative to the graph builder. Suitable when the control flow is better expressed as function calls than as explicit edges.

| Component | API | Description |
|---|---|---|
| **@entrypoint** | `@entrypoint(checkpointer=...) async def my_agent(state): ...` | Declares the top-level durable function. Behaves like a compiled graph — supports `invoke`, `stream`, `interrupt`, checkpointing. The function body IS the control flow. |
| **@task** | `@task async def call_llm(messages): return llm.invoke(messages)` | A unit of work that can run concurrently. Call multiple `@task` functions inside an `@entrypoint` and call `.result()` to await them. LangGraph runs them as a parallel fan-out. Equivalent to Send-based parallelism but in regular Python style. |

---

## 11. Observability & Deployment

| Component | API / Tool | Description |
|---|---|---|
| **LangSmith Tracing** | `LANGSMITH_TRACING=true` env var | Zero-code tracing. Every graph run is captured: nodes, state at each step, LLM calls, tool calls, latency. Enables debugging, evaluations, and regression testing on agent trajectories. |
| **LangSmith Studio** | UI at `smith.langchain.com` | Visual replay of any run. Edit state and re-run from any checkpoint. Share runs with teammates. Version-compare agent behavior across code changes. |
| **LangGraph Platform** | `langgraph deploy` / `langgraph.json` | Production deployment runtime. Provides: a REST API for graph invocation, SSE streaming, persistent threads, background runs, cron triggers, and a webhook system. No infra to manage. |
| **langgraph.json** | Project manifest | Declares graphs (`{"graphs": {"agent": "./agent.py:graph"}}`), Python version, dependencies, environment variables. Used by `langgraph dev` (local) and `langgraph deploy` (cloud). |
| **Background runs** | Platform API | Fire-and-forget execution. Client gets a `run_id` immediately; result is polled or received via webhook. Correct pattern for runs that take minutes. |
| **RetryPolicy** | `graph.add_node("name", fn, retry=RetryPolicy(max_attempts=3))` | Per-node retry with configurable backoff. Automatically retries on transient failures (network errors, rate limits). The exception type is checked against `retry_on` predicate. |

---

# Python Code Samples

## S1 · Minimal StateGraph — ReAct agent wired by hand

```python
# manual_react.py
from typing import Annotated
from langchain_openai import ChatOpenAI
from langchain_core.tools import tool
from langgraph.graph import StateGraph, START, END, MessagesState
from langgraph.prebuilt import ToolNode

# 1. Define a tool
@tool
def get_weather(city: str) -> str:
    """Return weather for a city."""
    return f"Sunny, 22°C in {city}"

tools = [get_weather]
llm = ChatOpenAI(model="gpt-4.1").bind_tools(tools)

# 2. Node functions — receive full state, return partial update
def call_llm(state: MessagesState):
    response = llm.invoke(state["messages"])
    return {"messages": [response]}  # add_messages reducer appends

def should_continue(state: MessagesState) -> str:
    """Router: does the last message have tool calls?"""
    last = state["messages"][-1]
    return "tools" if last.tool_calls else END

# 3. Build graph
builder = StateGraph(MessagesState)
builder.add_node("llm", call_llm)
builder.add_node("tools", ToolNode(tools))
builder.add_edge(START, "llm")
builder.add_conditional_edges("llm", should_continue, {"tools": "tools", END: END})
builder.add_edge("tools", "llm")   # always return to LLM after tool

graph = builder.compile()

# 4. Invoke
result = graph.invoke({"messages": [{"role": "user", "content": "Weather in Cupertino?"}]})
print(result["messages"][-1].content)
```

## S2 · Prebuilt `create_react_agent` — the fast path

For any standard tool-calling agent, skip the manual wiring above.

```python
# prebuilt_react.py
from langchain_openai import ChatOpenAI
from langchain_core.tools import tool
from langgraph.prebuilt import create_react_agent
from langgraph.checkpoint.memory import InMemorySaver

@tool
def search_docs(query: str) -> str:
    """Search internal documentation."""
    return f"[Doc result for: {query}]"

@tool
def run_query(sql: str) -> str:
    """Execute a read-only SQL query."""
    return f"[Result of: {sql}]"

checkpointer = InMemorySaver()
graph = create_react_agent(
    model=ChatOpenAI(model="gpt-4.1"),
    tools=[search_docs, run_query],
    prompt="You are an SRE assistant. Prefer read-only tools.",
    checkpointer=checkpointer,
)

config = {"configurable": {"thread_id": "user-42"}}

# Streaming token by token
for chunk, meta in graph.stream(
    {"messages": [{"role": "user", "content": "How many errors in the last hour?"}]},
    config,
    stream_mode="messages",  # token streaming
):
    if hasattr(chunk, "content") and chunk.content:
        print(chunk.content, end="", flush=True)
```

## S3 · Custom State + Reducers

```python
# custom_state.py
import operator
from typing import Annotated
from typing_extensions import TypedDict
from langgraph.graph import StateGraph, START, END
from langgraph.graph.message import add_messages
from langchain_core.messages import BaseMessage

class ResearchState(TypedDict):
    # Reducer: list grows monotonically — never overwritten
    messages: Annotated[list[BaseMessage], add_messages]
    # Reducer: numeric accumulator
    tokens_used: Annotated[int, operator.add]
    # No reducer: last writer wins
    final_report: str
    # No reducer: simple flag
    approved: bool

def researcher(state: ResearchState):
    # ... call LLM, get response ...
    return {
        "messages": [response],
        "tokens_used": response.usage_metadata["total_tokens"],
    }

def writer(state: ResearchState):
    return {"final_report": "# Report\n..."}

builder = StateGraph(ResearchState)
builder.add_node("researcher", researcher)
builder.add_node("writer", writer)
builder.add_edge(START, "researcher")
builder.add_edge("researcher", "writer")
builder.add_edge("writer", END)
graph = builder.compile()
```

## S4 · Persistence + Time Travel

```python
# persistence.py
from langgraph.checkpoint.memory import InMemorySaver
from langgraph.prebuilt import create_react_agent

checkpointer = InMemorySaver()
graph = create_react_agent(model="openai:gpt-4.1", tools=[...], checkpointer=checkpointer)

config = {"configurable": {"thread_id": "sess-1"}}

# Turn 1
graph.invoke({"messages": [{"role": "user", "content": "My name is Sandipan"}]}, config)

# Turn 2 — same thread_id; full message history is automatically restored
graph.invoke({"messages": [{"role": "user", "content": "What's my name?"}]}, config)

# --- Time travel ---
# List all checkpoints for this thread
history = list(graph.get_state_history(config))
for snap in history:
    print(snap.config["configurable"]["checkpoint_id"], snap.next)

# Re-run from checkpoint #2 (creates a new branch; doesn't modify original)
old_checkpoint_id = history[2].config["configurable"]["checkpoint_id"]
branch_config = {"configurable": {"thread_id": "sess-1", "checkpoint_id": old_checkpoint_id}}
result = graph.invoke({"messages": [{"role": "user", "content": "Try again from here"}]}, branch_config)
```

## S5 · Human-in-the-Loop: `interrupt()` + `Command(resume=...)`

```python
# hitl.py
from langgraph.types import interrupt, Command
from langgraph.graph import StateGraph, START, END
from langgraph.checkpoint.memory import InMemorySaver
from typing_extensions import TypedDict

class State(TypedDict):
    query: str
    sql: str
    result: str
    approved: bool

def generate_sql(state: State):
    # LLM generates SQL
    sql = f"SELECT * FROM logs WHERE error=true LIMIT 100"  # stub
    return {"sql": sql}

def human_review(state: State):
    """Pause and ask a human to approve the generated SQL."""
    approved = interrupt({
        "question": "Approve this SQL before execution?",
        "sql": state["sql"],
    })
    return {"approved": approved}

def execute_sql(state: State):
    if not state["approved"]:
        return {"result": "Blocked by human review"}
    return {"result": f"[rows from: {state['sql']}]"}

def route_after_review(state: State) -> str:
    return "execute" if state["approved"] else END

builder = StateGraph(State)
builder.add_node("generate", generate_sql)
builder.add_node("review", human_review)
builder.add_node("execute", execute_sql)
builder.add_edge(START, "generate")
builder.add_edge("generate", "review")
builder.add_conditional_edges("review", route_after_review, {"execute": "execute", END: END})
builder.add_edge("execute", END)

graph = builder.compile(checkpointer=InMemorySaver())
config = {"configurable": {"thread_id": "run-1"}}

# Phase 1: run until interrupt
for event in graph.stream({"query": "show errors", "sql": "", "result": "", "approved": False}, config):
    if "__interrupt__" in event:
        print("PAUSED. Payload:", event["__interrupt__"][0].value)
        break

# Human decides...
human_decision = True

# Phase 2: resume with decision
for event in graph.stream(Command(resume=human_decision), config):
    print(event)
```

## S6 · Send API — Parallel Fan-Out / Map-Reduce

```python
# send_fanout.py
import operator
from typing import Annotated
from typing_extensions import TypedDict
from langgraph.types import Send
from langgraph.graph import StateGraph, START, END

class OverallState(TypedDict):
    items: list[str]
    summaries: Annotated[list[str], operator.add]  # reducer collects results

class WorkerState(TypedDict):
    item: str

def router(state: OverallState):
    """Fan out: spawn one worker per item in parallel."""
    return [Send("worker", {"item": item}) for item in state["items"]]

def worker(state: WorkerState):
    """Process a single item. Runs in parallel with other workers."""
    summary = f"Summary of '{state['item']}'"  # stub: call LLM here
    return {"summaries": [summary]}  # reducer appends into OverallState

def aggregator(state: OverallState):
    """Fan-in: all workers have finished; summaries is now fully populated."""
    final = "\n".join(state["summaries"])
    print("FINAL:", final)
    return {}

builder = StateGraph(OverallState)
builder.add_node("worker", worker)
builder.add_node("aggregator", aggregator)
builder.add_conditional_edges(START, router, ["worker"])  # router returns Send list
builder.add_edge("worker", "aggregator")
builder.add_edge("aggregator", END)

graph = builder.compile()
graph.invoke({"items": ["report_A.pdf", "report_B.pdf", "report_C.pdf"], "summaries": []})
```

## S7 · Long-Term Memory with BaseStore

```python
# memory_store.py
from typing import Annotated
from langgraph.store.memory import InMemoryStore
from langgraph.store.base import BaseStore
from langgraph.prebuilt import InjectedStore, create_react_agent
from langchain_core.tools import tool

store = InMemoryStore()

@tool
def save_preference(preference: str, store: Annotated[BaseStore, InjectedStore]) -> str:
    """Save a user preference to long-term memory."""
    # store.put(namespace_tuple, key, value)
    store.put(("user", "alice", "prefs"), "general", {"preference": preference})
    return f"Saved: {preference}"

@tool
def recall_preferences(store: Annotated[BaseStore, InjectedStore]) -> str:
    """Retrieve user preferences from long-term memory."""
    item = store.get(("user", "alice", "prefs"), "general")
    return str(item.value) if item else "No preferences found."

graph = create_react_agent(
    model="openai:gpt-4.1",
    tools=[save_preference, recall_preferences],
    store=store,
    prompt=(
        "You are a personalized assistant. "
        "When the user states a preference, use save_preference. "
        "Before answering personal questions, check recall_preferences first."
    ),
)

# Thread 1
config_1 = {"configurable": {"thread_id": "session-1"}}
graph.invoke({"messages": [{"role": "user", "content": "I prefer dark mode"}]}, config_1)

# Thread 2 — completely different session, but same store
config_2 = {"configurable": {"thread_id": "session-2"}}
result = graph.invoke({"messages": [{"role": "user", "content": "What are my preferences?"}]}, config_2)
print(result["messages"][-1].content)  # Recalls from store, not chat history
```

## S8 · Multi-Agent Supervisor (manual, tool-calling pattern)

```python
# supervisor.py
from langchain_openai import ChatOpenAI
from langchain_core.tools import tool
from langgraph.prebuilt import create_react_agent
from langgraph.graph import MessagesState, StateGraph, START, END

model = ChatOpenAI(model="gpt-4.1")

# ── Specialist agents ────────────────────────────────────────────────
@tool
def search_web(query: str) -> str:
    """Search the web for current information."""
    return f"[web result for: {query}]"

@tool
def run_python(code: str) -> str:
    """Execute Python code and return the result."""
    return f"[python output: {code}]"

web_agent   = create_react_agent(model, tools=[search_web], name="web_agent")
code_agent  = create_react_agent(model, tools=[run_python], name="code_agent")

# ── Supervisor uses agents as tools ──────────────────────────────────
def make_handoff(agent_graph, name: str):
    """Wrap a subgraph as a tool the supervisor can call."""
    @tool(name=f"call_{name}", description=f"Delegate to {name}")
    def handoff(task: str) -> str:
        result = agent_graph.invoke({"messages": [{"role": "user", "content": task}]})
        return result["messages"][-1].content
    return handoff

supervisor_tools = [make_handoff(web_agent, "web_agent"), make_handoff(code_agent, "code_agent")]

supervisor_graph = create_react_agent(
    model=model,
    tools=supervisor_tools,
    prompt=(
        "You are a supervisor. For web lookups, call call_web_agent. "
        "For code execution, call call_code_agent. "
        "Synthesize the responses into a final answer."
    ),
)

result = supervisor_graph.invoke({
    "messages": [{"role": "user", "content": "What's today's NVDA stock price? Then calculate its P/E ratio assuming EPS of 11.93."}]
})
print(result["messages"][-1].content)
```

## S9 · Functional API — `@entrypoint` / `@task`

```python
# functional_api.py
from langgraph.func import entrypoint, task
from langgraph.checkpoint.memory import InMemorySaver
from langchain_openai import ChatOpenAI

llm = ChatOpenAI(model="gpt-4.1")

@task
def analyze_section(section: str) -> str:
    """Summarize one section of a document."""
    return llm.invoke(f"Summarize in one sentence: {section}").content

@task
def score_quality(summary: str) -> float:
    """Score the quality of a summary on 0-1."""
    verdict = llm.invoke(f"Rate this summary quality (reply with a float 0-1): {summary}").content
    return float(verdict.strip())

@entrypoint(checkpointer=InMemorySaver())
def analyze_document(sections: list[str]) -> dict:
    # Fan out: all analyze_section tasks run in parallel
    summary_futures = [analyze_section(s) for s in sections]
    summaries = [f.result() for f in summary_futures]

    # Fan out again: score each summary in parallel
    score_futures = [score_quality(s) for s in summaries]
    scores = [f.result() for f in score_futures]

    return {"summaries": summaries, "scores": scores, "avg_score": sum(scores) / len(scores)}

result = analyze_document.invoke(["Introduction...", "Methods...", "Results..."])
print(result)
```

---

# Design Decision Quick-Reference

| Question | Answer |
|---|---|
| **create_react_agent vs manual StateGraph?** | Start with `create_react_agent`. Migrate to manual only when you need: parallel node execution, a supervisor, non-message state fields, custom retry logic, or branching not expressible as a single conditional edge. |
| **TypedDict vs Pydantic for state?** | TypedDict for simplicity. Pydantic when you need runtime validation, default values, or computed fields. |
| **How do I add a field without overwriting?** | Annotate with a reducer: `Annotated[list, operator.add]` or a custom merge function. |
| **Checkpointer in production?** | `PostgresSaver`. Never ship `InMemorySaver`; it resets on every restart. |
| **Parallelism?** | `Send` API (graph builder) or `@task` (functional API). Both run branches concurrently and merge with reducers. |
| **HITL?** | `interrupt()` for conditional/dynamic pauses. `interrupt_before/after` at compile time for unconditional gates. |
| **Cross-session memory?** | `BaseStore` (not the checkpointer). Inject via `InjectedStore` in tools or nodes. |
| **Token streaming?** | `stream_mode="messages"` — yields `(AIMessageChunk, metadata)` tuples. |
| **Observability?** | Set `LANGSMITH_TRACING=true`. Every run is captured automatically with no code changes. |
| **Subgraph events not visible?** | Pass `subgraphs=True` to `stream()`. |

---

# What LangGraph Does NOT Do (Intentionally)

| Non-goal | Alternative |
|---|---|
| Abstracts prompt engineering | You write your own prompts; LangGraph executes the graph |
| Provides a fixed "agent cognitive architecture" | You design the topology; LangGraph runs it |
| LLM provider SDK | Use LangChain integrations (`langchain-openai`, etc.) or call any provider directly |
| RAG pipelines | Use LangChain retrievers or call your vector DB directly from a node |
| Evaluation / evals | LangSmith |

---

*Inspired by Pregel (Google) and Apache Beam. Public graph API inspired by NetworkX. Built by LangChain Inc (open-source, MIT). Trusted in production by Klarna, Replit, Uber, LinkedIn, JP Morgan.*
