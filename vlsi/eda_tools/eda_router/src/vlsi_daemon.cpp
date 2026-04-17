// ═══════════════════════════════════════════════════════════════════════════════
// FILE: vlsi_daemon.cpp  —  The Master Orchestrator Entry Point
// ═══════════════════════════════════════════════════════════════════════════════
//
// ┌─────────────────────────────────────────────────────────────────────────┐
// │                  COMPLETE ENGINE FLOW DIAGRAM                          │
// │                                                                         │
// │  Python LangGraph Agent (the "General")                                 │
// │       │                                                                 │
// │       │ JSON-RPC over WebSocket                                         │
// │       ▼                                                                 │
// │  WebSocketServer (transport layer)                                      │
// │       │                                                                 │
// │       ├── "load_design" → DbMcpServer::load_design()                   │
// │       │       │                                                         │
// │       │       └── RoutingGridGraph::build_lattice()                     │
// │       │             (builds the Boost.Graph 3D lattice)                 │
// │       │                                                                 │
// │       └── "route_nets"  → RoutingMcpServer::route_nets()               │
// │               │                                                         │
// │               └── RoutingPipelineOrchestrator::run()                   │
// │                     │                                                   │
// │                     ├── 1. DesignAnalyzer::analyze()                   │
// │                     │       → DesignSummary (context classification)   │
// │                     │                                                   │
// │                     ├── 2. CongestionOracle::rebuild()                 │
// │                     │       → O(1) demand lookup cache                 │
// │                     │                                                   │
// │                     ├── 3. PinAccessOracle::precompute()               │
// │                     │       → Legal pin approach vectors               │
// │                     │                                                   │
// │                     ├── 4. DRCPenaltyModel::apply_masks()             │
// │                     │       → EdgeProperties::drc_mask bits set        │
// │                     │                                                   │
// │                     ├── 5. ElectricalConstraintEngine::precompute()    │
// │                     │       → EdgeProperties::w_elec set               │
// │                     │                                                   │
// │                     ├── 6. GlobalPlanner::plan()   [GA loop]           │
// │                     │       → CorridorAssignment (one bbox per net)    │
// │                     │                                                   │
// │                     ├── 7. CorridorRefinement::refine()                │
// │                     │       → Verify via sites + capacity feasibility  │
// │                     │       → infeasible corridors → penalize GA       │
// │                     │                                                   │
// │                     ├── 8. SpatialPartitioner::partition()             │
// │                     │       → N PartitionRegion strips                 │
// │                     │       → ghost cells marked on boundary            │
// │                     │                                                   │
// │                     ├── 9. NegotiatedRoutingLoop::converge()           │
// │                     │       → Per-pass A* routing (DetailedGridRouter) │
// │                     │       → ConvergenceMonitor: detect oscillation   │
// │                     │       → IlpSolver: resolve oscillating subregion │
// │                     │       → HistoryCostUpdater: congestion + DRC     │
// │                     │       → AdaptivePenaltyController: tune W_cong   │
// │                     │       → CrossRegionMediator: boundary nets       │
// │                     │                                                   │
// │                     └── 10. RouteEvaluator::evaluate()                 │
// │                             → wirelength, via count, DRC violations    │
// │                             → EvaluationReport → JSON response         │
// └─────────────────────────────────────────────────────────────────────────┘
//
// HOW THIS INTERACTS WITH THE PYTHON ORCHESTRATOR AGENT (LangGraph):
//
//   The Python LangGraph agent (in python_programs/agentic_ai_projects/langgraph/)
//   acts as the "General" that receives design intent from the user and breaks
//   it into tool calls.  Each tool call arrives as a JSON-RPC message over WebSocket.
//
//   TYPICAL AGENT INTERACTION SEQUENCE:
//     1. Python agent receives: "Route this design, minimize via count"
//     2. Agent decides:
//        a. Send "load_design": {"filename": "my_chip.def"}
//        b. Wait for confirmation: {"result": "48000 vertices, 168800 edges"}
//        c. Send "route_nets": {"strategy": "minimize_vias", "max_passes": 30}
//        d. Wait for: {"wirelength": 82540, "via_count": 1204, "drc_violations": 0}
//     3. Agent reports results back to user
//
//   The VLSI Daemon never needs to know about the agent's reasoning.
//   The agent never needs to know about individual A* expansions.
//   They communicate only through JSON-RPC messages — a clean MCP interface.
//
// ─────────────────────────────────────────────────────────────────────────────

#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <nlohmann/json.hpp>

// ─── MCP Transport + Server Layer ─────────────────────────────────────────────
// These provide the WebSocket JSON-RPC interface that the Python agent calls.
#include "routing_genetic_astar/transport/websocket_transport.hpp"
#include "routing_genetic_astar/mcp/servers/db_mcp_server.hpp"
#include "routing_genetic_astar/mcp/servers/routing_mcp_server.hpp"

// ─── Shared State ─────────────────────────────────────────────────────────────
// SharedDatabase is the single object that both MCP servers read/write.
// It holds the RoutingGridGraph and the loaded design metadata.
#include "routing_genetic_astar/shared_database.hpp"

using json = nlohmann::json;

// ═══════════════════════════════════════════════════════════════════════════════
// FUNCTION: print_startup_banner
// PURPOSE: Print a human-readable connection guide when the daemon starts.
//          The Python agent reads the port from here.
// ═══════════════════════════════════════════════════════════════════════════════
static void print_startup_banner() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════╗\n";
    std::cout << "║          VLSI Routing Daemon v3 (C++23)             ║\n";
    std::cout << "╠══════════════════════════════════════════════════════╣\n";
    std::cout << "║  WebSocket MCP server: ws://127.0.0.1:8080          ║\n";
    std::cout << "║                                                      ║\n";
    std::cout << "║  Supported JSON-RPC Methods:                         ║\n";
    std::cout << "║    ping         → health check                       ║\n";
    std::cout << "║    load_design  → parse DEF/LEF, build lattice       ║\n";
    std::cout << "║    route_nets   → run full GA+PathFinder pipeline    ║\n";
    std::cout << "╚══════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
}

// ═══════════════════════════════════════════════════════════════════════════════
// FUNCTION: handle_request
// PURPOSE: Dispatches an incoming JSON-RPC request string to the right server.
//
// WHAT IT DOES (step by step):
//   1. Parse the incoming string as JSON.
//   2. Read the "method" field from the JSON object.
//   3. Dispatch to the correct MCP server based on the method name.
//   4. Return the JSON response as a string.
//   5. If ANYTHING throws (bad JSON, unknown method, logic errors), catch it
//      and return a properly-formatted JSON-RPC error response.
//
// WHY A SEPARATE FUNCTION?
//   Separating the dispatch logic from the WebSocket transport makes the code
//   testable: we can call handle_request() directly in unit tests without
//   needing a real live WebSocket connection.
//
// ─── C++ FEATURE: const& (Const Reference) ────────────────────────────────────
// `const std::string& msg` passes the string BY REFERENCE (no copy) and marks
// it CONST (we promise not to modify it).
// ANALOGY: Reading a memo pinned to a corkboard without taking it off the wall.
// If we took it by VALUE, the OS would make a full copy of potentially large
// JSON strings just to route one request — wasteful.
// ─────────────────────────────────────────────────────────────────────────────
static std::string handle_request(const std::string& msg,
                                  mcp::DbMcpServer&         db_server,
                                  mcp::RoutingMcpServer&    routing_server) {
    try {
        // Parse incoming JSON string.
        // json::parse throws json::parse_error if the string is not valid JSON.
        auto req = json::parse(msg);

        const std::string method = req.value("method", "");

        // ─── ping: simple health check so the Python agent can verify the daemon is alive.
        if (method == "ping") {
            return json{{"jsonrpc", "2.0"}, {"result", "pong"}}.dump();
        }

        // ─── load_design: parses the design file and builds the Boost.Graph lattice.
        //   Flow: JSON → DbMcpServer → RoutingGridGraph::build_lattice()
        //   Response contains vertex count and edge count so Python can log progress.
        else if (method == "load_design") {
            return db_server.load_design(req.value("params", json{})).dump();
        }

        // ─── route_nets: runs the full V3 routing pipeline.
        //   Flow: JSON → RoutingMcpServer → RoutingPipelineOrchestrator::run()
        //   Response contains EvaluationReport metrics.
        else if (method == "route_nets") {
            return routing_server.route_nets(req.value("params", json{})).dump();
        }

        // ─── Unknown method: return JSON-RPC error as spec requires.
        else {
            return json{
                {"jsonrpc", "2.0"},
                {"error", {{"code", -32601}, {"message", "Method not found: " + method}}}
            }.dump();
        }

    } catch (const std::exception& e) {
        // Catch-all: any C++ exception becomes a JSON-RPC error response.
        // The Python agent sees the error message string and can retry or escalate.
        return json{
            {"jsonrpc", "2.0"},
            {"error", {{"code", -32000}, {"message", std::string(e.what())}}}
        }.dump();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// FUNCTION: main
// PURPOSE: Entry point.  Wires together all components and starts the daemon.
//
// CODE FLOW:
//   1. Create SharedDatabase (the shared memory holding RoutingGridGraph).
//   2. Create DbMcpServer and RoutingMcpServer (both share the same DB pointer).
//   3. Create WebSocketServer on port 8080.
//   4. Register the dispatch lambda as the message handler.
//   5. Start the server (blocking).  Server processes requests indefinitely until
//      SIGINT (Ctrl+C) or SIGTERM from the OS.
//
// ─── C++ FEATURE: std::make_shared ────────────────────────────────────────────
// `std::make_shared<T>(args...)` creates an object of type T on the heap and
// wraps it in a shared_ptr — a "smart pointer" that automatically deletes the
// object when the last shared_ptr referencing it is destroyed.
//
// ANALOGY: A shared office coffee machine with a counter showing how many people
// are using it.  When the last person leaves (counter → 0), the machine powers
// off automatically.  No manual shutdown needed.
//
// WHY SHARED_PTR FOR SharedDatabase?
//   Both DbMcpServer AND RoutingMcpServer need to access the SAME database.
//   If we used raw pointers, we'd need to manually manage ownership.
//   With shared_ptr, both servers hold a reference to the SAME database object.
//   When both servers are destroyed, the database is automatically freed.
// ─────────────────────────────────────────────────────────────────────────────
int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    print_startup_banner();

    // STEP 1: Shared database owns the RoutingGridGraph and design metadata.
    //         Both MCP servers read/write this object via shared ownership.
    auto shared_db = std::make_shared<SharedDatabase>();

    // STEP 2: MCP Servers — each handles a category of JSON-RPC methods.
    //         They share a pointer to shared_db so changes in one are visible to the other.
    mcp::DbMcpServer      db_server{shared_db};
    mcp::RoutingMcpServer routing_server{shared_db};

    // STEP 3: WebSocket transport layer — listens for incoming JSON-RPC messages.
    std::cout << "[Daemon] Creating WebSocket listener on ws://127.0.0.1:8080...\n";
    transport::WebSocketServer server("127.0.0.1", 8080);

    // STEP 4: Register message handler — the dispatch lambda.
    //
    // ─── C++ FEATURE: Lambda Captures with [&] ────────────────────────────────
    // A lambda is an anonymous (unnamed) function defined inline.
    // [&] means "capture by reference": the lambda has access to all local
    // variables in this scope (db_server, routing_server) by reference.
    // ANALOGY: A sticky note inside an envelope that can see everything on the desk
    // where it was written — it doesn't copy the desk, just reads it.
    // ─────────────────────────────────────────────────────────────────────────
    server.set_message_handler([&](const std::string& msg) -> std::string {
        return handle_request(msg, db_server, routing_server);
    });

    std::cout << "[Daemon] WebSocket server running. Waiting for agent connections...\n";
    std::cout << "[Daemon] Send Ctrl+C to stop.\n\n";

    // STEP 5: Blocking server loop. Runs until the process is killed.
    server.run();

    std::cout << "[Daemon] Shutdown complete.\n";
    return 0;
}
