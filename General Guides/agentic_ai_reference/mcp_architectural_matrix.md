# The MCP Architectural Matrix — Comprehensive Reference

**Spec baseline:** MCP `2025-11-25` (latest stable, governed by the Agentic AI Foundation under the Linux Foundation since Dec 2025).
**Python stack assumed:** `mcp` SDK (FastMCP integrated), `openai-agents` for Host orchestration.

Your original matrix was solid on topology and primitives but predated several of the largest changes: **Streamable HTTP** replacing HTTP+SSE, **Elicitation** as a first-class client primitive, **Structured Tool Output**, **Tool Annotations**, and **Tasks** (experimental async). Missing cross-cutting features — logging, completion, pagination, cancellation, subscriptions, capability negotiation — are called out below.

---

## 1. Topology & Wire Protocol

| Component | Layer | Decision Maker | Description |
|---|---|---|---|
| **Host** | Application | System Developer | Orchestrator app (Claude Desktop, Cursor, your Python script). Holds LLM API keys, chat history, executes the agentic loop, enforces user consent. |
| **Client** | `mcp.ClientSession` inside Host | Host | Protocol adapter. One Client per connected Server. Translates Host intent into JSON-RPC 2.0, owns the transport lifecycle. |
| **Server** | External process or remote service | Follows commands | Stateless (mostly) capability provider. Contains no LLM. Exposes primitives, may call *back* into the Host (sampling/elicitation/roots). |
| **Transport: stdio** | Wire | Host launches subprocess | Local only. Host spawns the Server as a child process and pipes JSON-RPC over stdin/stdout. Zero config, zero auth, fastest for local tools. |
| **Transport: Streamable HTTP** | Wire | Host connects via URL | **The remote standard as of 2025.** Single HTTP endpoint that supports POST (request/response) and optional SSE upgrade (streaming). Replaces the older HTTP+SSE split transport. Carries OAuth bearer tokens. |
| **JSON-RPC 2.0** | Message format | Both | Every MCP message is a JSON-RPC `request`, `response`, or `notification`. Requests have `id`; notifications don't. |
| **Lifecycle** | Session state machine | Both | `initialize` → `initialized` notification → normal operation → `shutdown`. No protocol work is legal before the init handshake completes. |
| **Capability Negotiation** | In `initialize` | Both sides declare | Each side advertises which features it supports (`tools`, `resources`, `prompts`, `sampling`, `elicitation`, `roots`, `logging`, etc.). You must NOT call a feature the peer didn't advertise. |
| **`_meta` fields** | Any message | Extension escape hatch | Reserved object on most request/response shapes for vendor-specific metadata without breaking forward compatibility. |

---

## 2. Server Primitives (Server → Host/LLM)

| Primitive | Decorator | Decision Maker | Description & Gotchas |
|---|---|---|---|
| **Tools** | `@mcp.tool()` | **LLM** | Executable, potentially side-effecting functions. LLM sees name + JSON schema + description, decides to call. Treat descriptions as untrusted input unless the Server is trusted. |
| **Tool Annotations** | `annotations=` on `@mcp.tool` | Host surfaces to user | Hints: `readOnlyHint`, `destructiveHint`, `idempotentHint`, `openWorldHint`, `title`. These are **hints, not guarantees** — Hosts use them for UI and consent prompts (e.g., an extra confirmation on destructive tools). |
| **Structured Tool Output** | Return-type annotation + `outputSchema` | Server | Since spec 2025-06-18. Dict / Pydantic / dataclass returns auto-produce `structuredContent` JSON alongside traditional text content. Primitive returns need a `-> int` style annotation to generate the schema. Suppress with `structured_output=False`. |
| **Resources** | `@mcp.resource("uri")` | **Host** | Read-only context (files, DB rows, configs). LLM doesn't see resource *list* automatically — Host chooses which to read and injects into prompt. Static URI or URI template (e.g., `users://{id}/profile`). |
| **Resource Templates** | `@mcp.resource("uri://{param}")` | Host (parameterized) | Parameterized resources. Host resolves the template with concrete args, then reads. Enables "one decorator, many resources." |
| **Resource Subscriptions** | `resources/subscribe` + `notifications/resources/updated` | Host subscribes, Server pushes | Host subscribes to a URI; Server notifies on change. Enables live-updating context (DB row, file watch) without polling. |
| **Prompts** | `@mcp.prompt()` | **User** | Pre-packaged, parameterized message templates. User-triggered (slash commands in Claude Desktop). Return `list[PromptMessage]`. Different from tools: users pick them, LLM doesn't. |

---

## 3. Client Primitives (Server → Host callbacks)

These run in the *reverse* direction. The Server requests; the Host fulfills. The Host must advertise support in the initialize handshake.

| Primitive | API | Decision Maker | Description |
|---|---|---|---|
| **Sampling** | `ctx.session.create_message(...)` | **Server requests, Host's LLM executes** | Server asks the Host to run an LLM completion on its behalf. Keeps massive intermediate data (50-page log summarization, map-reduce chunking) out of the Host's chat history. Server can pass `ModelPreferences` (speed vs intelligence vs cost) to steer the Host toward the right model. |
| **Elicitation (form mode)** | `ctx.elicit(message=..., schema=Pydantic)` | **Server requests, User answers via Host UI** | Newer than sampling. Server mid-tool-call asks the user a structured question ("Confirm booking? Notes?"). Host renders a form from the schema. Returns `AcceptedElicitation` / `DeclinedElicitation` / `CancelledElicitation`. Schema must be **primitive types only** (str/int/float/bool) — no nested objects. |
| **Elicitation (URL mode)** | `ElicitRequestURLParams` | Server requests, user visits URL | For sensitive flows (OAuth, payment, credentials). Server returns a URL; Host opens it in a browser; user completes off-channel. Never transits MCP with secrets. |
| **Roots** | `session.list_roots()` | Host defines, Server queries | Filesystem/URI boundaries the Host has authorized. Server *should* check before touching paths — it's the anti-path-traversal handshake. For Docker-sandboxed agents, roots map to mounted volumes that persist across ephemeral containers. |

---

## 4. Cross-Cutting Utilities

| Feature | API | Direction | Description |
|---|---|---|---|
| **Logging** | `ctx.info/debug/warning/error(...)` + `logging/setLevel` | Server → Host | Structured, leveled logs surfaced in the Host UI. Host sets min level via `logging/setLevel`. Not `print()` — that contaminates stdio transport. |
| **Progress** | `ctx.report_progress(progress, total, message)` | Server → Host | Progress tokens on long-running tool calls. Host renders a bar. Prevents user-facing "is it hung?" anxiety and keeps transport alive. |
| **Cancellation** | `notifications/cancelled` | Either direction | Caller can cancel an in-flight request by ID. Server should clean up and not send a response. Essential for runaway tool calls and user Ctrl-C. |
| **Completion** | `completion/complete` | Host → Server | Argument autocompletion for prompt/resource-template parameters. E.g., typing `@github/{repo}` — Server suggests repo names. Like IDE autocomplete for MCP. |
| **Pagination** | `cursor` param on all `list_*` methods | Host → Server | `tools/list`, `resources/list`, `prompts/list` all support cursor-based pagination. Servers with 10k tools don't dump them in one response. |
| **Ping** | `ping` request | Either direction | Keep-alive / liveness check. Transport-level health. |
| **List-Changed Notifications** | `notifications/tools/list_changed` (also resources, prompts) | Server → Host | Server tells Host the capability set changed; Host re-fetches. Enables dynamic tools (e.g., a server that exposes different tools after login). |

---

## 5. Experimental & Emerging (2026 roadmap)

| Feature | SEP | Status | Description |
|---|---|---|---|
| **Tasks** | SEP-1686 | Experimental, shipping in prod clients | First-class async operations. Create a task, poll or subscribe for completion, with retry semantics and expiry policies. 2026 roadmap is iterating on the lifecycle gaps (retry on transient failure, result retention TTL). Targets long-running agentic work that exceeds a single HTTP request. |
| **Server Cards** | `/.well-known/mcp-server` | Landed 2025 | Static metadata endpoint for discovery — name, version, capabilities, icon — served over HTTP without opening a session. Enables registries and crawlers. |
| **MCP Apps** | `ext-apps` repo | Experimental | Embedded interactive UIs served *by* MCP servers and rendered inside the Host's chat UI. Think: a widget from the Shopify server rendered inline in Claude. |
| **Interceptors** | SEP-1763 | Experimental | Middleware pattern — intercept requests/responses for auth, rate limiting, audit, transformation. Enables MCP gateways. |
| **Skills over MCP** | `experimental-ext-skills` | Exploratory | Package and distribute reusable agent skills through MCP primitives. |

---

## 6. Security, Consent & Enterprise

| Concern | Mechanism | Owner |
|---|---|---|
| **User Consent** | Host-enforced. Host MUST prompt before any tool call, especially destructive ones. | Host |
| **OAuth 2.1** | Authorization Code + PKCE over Streamable HTTP. Server advertises metadata at `/.well-known/oauth-authorization-server`. | Server + Host |
| **Roots as sandbox** | Server queries Host's allowed paths; operations outside are refused. | Host defines, Server respects |
| **Tool description trust** | Treat Server-supplied tool descriptions as untrusted input to the LLM (prompt injection vector) unless Server is trusted. | Host |
| **Tenant isolation** | Per-session state in Server; Hosts SHOULD spin up fresh sessions per user. | Server implementer |

---

## 7. High-Level Host Patterns

| Pattern | Where | Description |
|---|---|---|
| **Agent** (e.g., `openai-agents.Agent`) | Host | Static blueprint: persona (`instructions`) + model + `mcp_servers=[...]`. Capabilities are discovered at run time. |
| **Runner** (e.g., `openai-agents.Runner`) | Host | The execution loop. Translates MCP tool schemas into the LLM provider's tool-call format, routes tool calls back through `ClientSession`, feeds results to the LLM. Hides the JSON-RPC plumbing. |
| **FastMCP** | Server-side framework | Decorator-based Server SDK. Auto-generates JSON schemas from type hints, handles transport/lifecycle/validation. Incorporated into the official `mcp` Python SDK in 2024; now powers ~70% of MCP servers. |
| **Context object** | Server tools | Injected `ctx: Context` gives tool functions access to `elicit()`, `report_progress()`, `info()/debug()`, `session.create_message()`, `session.list_roots()`. This is *the* Server-side API surface for client primitives. |

---

## 8. Memory Patterns (unchanged but refined)

| Pattern | Where it lives | Decision Maker |
|---|---|---|
| **Agentic Memory** (Knowledge Graph) | Server exposes `add_nodes` / `read_graph` / `search_nodes` tools | **LLM** curates. Requires explicit system-prompt rules telling the LLM *when* to write/read (otherwise it forgets to use it). |
| **Implicit Memory** (Host-driven RAG) | Host runs vector search before the LLM call | **Host**. Grabs top-k, injects into system prompt as if it were a Resource. LLM sees context, doesn't know it's memory. |
| **Hybrid** | Both | Most production systems. Implicit for bulk recall, agentic for deliberate "remember this" events. |

---

# Python Code Samples (High-Level APIs)

## 8.1 Minimal FastMCP Server — Tools, Resources, Prompts, Structured Output

```python
# server.py
from pydantic import BaseModel
from mcp.server.fastmcp import FastMCP

mcp = FastMCP("demo-server")

# --- TOOL with structured output (auto-derived from return type) ---
class WeatherReport(BaseModel):
    city: str
    temp_c: float
    conditions: str

@mcp.tool(
    title="Get Weather",
    annotations={"readOnlyHint": True, "openWorldHint": True},
)
def get_weather(city: str) -> WeatherReport:
    """Fetch current weather for a city."""
    # Returning a Pydantic model -> FastMCP auto-generates outputSchema
    # and emits both structuredContent AND a text block (back-compat).
    return WeatherReport(city=city, temp_c=18.5, conditions="partly cloudy")

# --- RESOURCE TEMPLATE ---
@mcp.resource("users://{user_id}/profile")
def user_profile(user_id: str) -> str:
    """Profile document for a user."""
    return f"# Profile\nUser: {user_id}\nPlan: Pro"

# --- PROMPT (user-triggered via slash command) ---
@mcp.prompt()
def code_review(language: str, focus: str = "bugs") -> str:
    return f"Review the following {language} code. Focus on {focus}."

if __name__ == "__main__":
    # stdio for local; "streamable-http" for remote
    mcp.run(transport="stdio")
```

## 8.2 Server with Elicitation, Sampling, Progress, Logging

This shows all four client-primitive callbacks from inside one tool.

```python
# advanced_server.py
from pydantic import BaseModel, Field
from mcp.server.fastmcp import FastMCP, Context
from mcp.server.elicitation import (
    AcceptedElicitation, DeclinedElicitation, CancelledElicitation,
)
from mcp.types import SamplingMessage, TextContent

mcp = FastMCP("advanced-demo")

class Confirm(BaseModel):
    proceed: bool = Field(description="Run the expensive analysis?")
    notes: str = Field(default="", description="Optional notes")

@mcp.tool()
async def analyze_logs(path: str, ctx: Context) -> str:
    """Summarize a large log file with user confirmation + LLM sampling."""

    # 1. ROOTS — check path is allowed before touching disk
    roots = await ctx.session.list_roots()
    if not any(path.startswith(r.uri.removeprefix("file://")) for r in roots.roots):
        return f"Refused: {path} is outside allowed roots."

    # 2. ELICITATION — ask the user to confirm before doing expensive work
    confirmation = await ctx.elicit(
        message=f"About to analyze {path}. This may take several minutes.",
        schema=Confirm,
    )
    match confirmation:
        case AcceptedElicitation(data=d) if d.proceed:
            await ctx.info(f"Starting analysis. User notes: {d.notes!r}")
        case AcceptedElicitation():
            return "User declined to proceed."
        case DeclinedElicitation() | CancelledElicitation():
            return "Cancelled."

    # 3. PROGRESS — emit updates during long work
    chunks = _read_chunks(path)  # imagine 10 chunks
    summaries = []
    for i, chunk in enumerate(chunks):
        await ctx.report_progress(
            progress=i, total=len(chunks), message=f"chunk {i+1}"
        )
        # 4. SAMPLING — offload summarization to Host's LLM, cheap model
        result = await ctx.session.create_message(
            messages=[SamplingMessage(
                role="user",
                content=TextContent(type="text", text=f"Summarize:\n{chunk}"),
            )],
            max_tokens=200,
            model_preferences={"speed_priority": 0.8, "intelligence_priority": 0.2},
        )
        summaries.append(result.content.text)

    await ctx.report_progress(progress=len(chunks), total=len(chunks))
    return "\n\n".join(summaries)

def _read_chunks(path): ...  # stub

if __name__ == "__main__":
    mcp.run(transport="stdio")
```

Key point: `create_message` does map-reduce **without** the intermediate chunk text ever appearing in the Host's chat history. Only the final joined summary does.

## 8.3 High-Level Client via `openai-agents`

The Agent+Runner pattern. You never write a JSON-RPC message by hand.

```python
# agent_host.py
import asyncio
from agents import Agent, Runner
from agents.mcp import MCPServerStdio, MCPServerStreamableHttp

async def main():
    # Local server launched as subprocess
    local = MCPServerStdio(
        params={"command": "python", "args": ["server.py"]},
    )
    # Remote server over Streamable HTTP with OAuth
    remote = MCPServerStreamableHttp(
        params={
            "url": "https://mcp.example.com/mcp",
            "headers": {"Authorization": "Bearer $TOKEN"},
        },
    )

    async with local, remote:
        agent = Agent(
            name="Ops Assistant",
            instructions=(
                "You are an SRE helper. Use the weather tool for location "
                "queries and the logs tool for investigation. Always prefer "
                "read-only tools first."
            ),
            model="gpt-4.1",  # or "claude-opus-4-7" via appropriate provider
            mcp_servers=[local, remote],
        )

        result = await Runner.run(
            agent,
            input="What's the weather in Cupertino? Then check /var/log/app.log.",
        )
        print(result.final_output)

asyncio.run(main())
```

The Runner handles: capability negotiation, schema translation to OpenAI tool-call format, routing tool calls to the correct MCPServer instance, marshalling results back, and the agent loop.

## 8.4 Low-Level `mcp.ClientSession` — when you need full control

```python
# low_level_client.py
import asyncio
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

async def main():
    params = StdioServerParameters(command="python", args=["server.py"])

    async with stdio_client(params) as (read, write):
        async with ClientSession(read, write) as session:
            # 1. Lifecycle
            await session.initialize()

            # 2. Discovery (with pagination)
            tools = await session.list_tools()
            print([t.name for t in tools.tools])

            # 3. Call a tool with structured output
            result = await session.call_tool("get_weather", {"city": "Cupertino"})
            # result.structuredContent -> dict matching WeatherReport schema
            # result.content -> list of TextContent/ImageContent blocks

            # 4. Read a resource
            contents = await session.read_resource("users://alice/profile")

            # 5. Run a user-triggered prompt
            prompt = await session.get_prompt("code_review",
                                              {"language": "python"})

            # 6. Subscribe to a resource
            await session.subscribe_resource("users://alice/profile")
            # ... notifications/resources/updated arrives asynchronously

asyncio.run(main())
```

## 8.5 Agentic Memory wiring (Knowledge Graph pattern)

Shows why the Agent's `instructions` are load-bearing:

```python
agent = Agent(
    name="Memory-aware assistant",
    model="gpt-4.1",
    mcp_servers=[memory_server, work_server],
    instructions="""
You have access to a persistent knowledge graph via these tools:
- search_nodes(query): recall before answering
- add_nodes(entities): store new durable facts
- add_edges(relations): link entities

MEMORY PROTOCOL (non-negotiable):
1. Before answering any personal question about the user, CALL search_nodes first.
2. When the user states a new preference, fact about themselves, or decision,
   CALL add_nodes to persist it — do not rely on the chat context.
3. Never mention the memory system to the user unless asked.
""",
)
```

Without those explicit rules, the LLM treats the memory tools as optional and forgets to use them. The Server side can expose perfect tools and the system still fails.

---

# Quick-Reference Checklist

When designing an MCP integration, walk these:

- [ ] Which primitive? **Tool** if the LLM decides. **Resource** if the Host injects context. **Prompt** if the user triggers.
- [ ] Transport: **stdio** for local, **Streamable HTTP** for remote. (HTTP+SSE is legacy — don't start new work there.)
- [ ] Auth: stdio needs none; HTTP means OAuth 2.1 + PKCE.
- [ ] Does the Server need to ask the user something mid-tool? → **Elicitation**.
- [ ] Does the Server need to offload summarization to the LLM? → **Sampling**.
- [ ] Long-running? → **Progress** + eventually **Tasks** (when stable).
- [ ] Returning structured data? → Type-annotate the return; FastMCP generates `outputSchema` automatically.
- [ ] Dangerous tool? → Set `destructiveHint: True` annotation so the Host prompts for consent.
- [ ] Capability gating: always check peer capabilities after `initialize` before calling advanced features.
- [ ] Memory: decide Host-RAG vs agent-curated. If agent-curated, bake the protocol into `instructions`.

---

# What Changed vs Your Original Matrix

| Your original | Updated |
|---|---|
| stdio as the implicit default; mentions "transport layer" generically | Streamable HTTP called out as the remote standard; SSE-only transport is legacy |
| No Elicitation | Elicitation added as a first-class client primitive (form + URL modes) |
| Tools described with schema only | Structured Output (2025-06-18 spec) and Tool Annotations added |
| Sampling covered | Still covered, with `ModelPreferences` example |
| No Logging, Progress, Completion, Pagination, Cancellation, Subscriptions, Ping | All added under cross-cutting utilities |
| No lifecycle / capability negotiation row | `initialize` handshake + capability gating are now explicit |
| No Tasks, Server Cards, MCP Apps, Interceptors | Experimental / roadmap section added |
| No OAuth / consent / tenant-isolation row | Security section added |
| Roots described for sandboxing | Same, plus explicit Host-defines / Server-queries framing |

---

*Governance note:* MCP is now an Agentic AI Foundation project under the Linux Foundation (donated Dec 2025). Spec evolution goes through Working Groups and SEPs. The 2026 roadmap prioritizes transport scalability, agent-to-agent communication, governance, and enterprise readiness (audit trails, SSO, gateways, config portability).
