// ═══════════════════════════════════════════════════════════════════════════════
// FILE: eda_cli/src/eda_daemon.cpp  ←  THE CORE OF THE EDA CLI DAEMON
// ═══════════════════════════════════════════════════════════════════════════════
//
// ─── HOW THIS FILE FITS INTO THE SYSTEM ──────────────────────────────────────
//
//  KLayout chatbot dock
//    │ HTTP POST /chat
//    ▼
//  Python server.py (FastAPI)
//    │ ainvoke() Graph A orchestrator
//    ▼
//  Python module subgraph (m1..m4) or workflow (w1,w2)
//    │ await mcp_call("route_nets", params)
//    │ WebSocket JSON-RPC over ws://127.0.0.1:8080
//    ▼
//  THIS FILE: eda_daemon.cpp
//    Receives JSON-RPC request
//    Parses method name (e.g. "route_nets")
//    Dispatches to the correct MCP server
//    Returns JSON-RPC result
//    │
//    ├─ "load_design"    → DbMcpServer::load_design()
//    ├─ "db.status"      → DbMcpServer::status()
//    ├─ "db.query_nets"  → DbMcpServer::query_nets()
//    ├─ "db.query_cells" → DbMcpServer::query_cells()
//    ├─ "db.query_bbox"  → DbMcpServer::query_bbox()
//    ├─ "db.net_bbox"    → DbMcpServer::net_bbox()
//    ├─ "route_nets"     → RoutingMcpServer::route_nets()
//    ├─ "drc.check"      → RoutingMcpServer::check_drc()
//    ├─ "eco.fix_drc"    → RoutingMcpServer::eco_fix_drc()
//    ├─ "place_cells"    → PlacerMcpServer::place_cells()
//    └─ "ping"           → returns "pong"
//
// ─── WHAT IS JSON-RPC 2.0? ───────────────────────────────────────────────────
//   A protocol where every request is:
//     {"jsonrpc":"2.0", "method":"route_nets", "params":{...}, "id":1}
//   And every response is EITHER:
//     {"jsonrpc":"2.0", "result":{...}, "id":1}         ← success
//     {"jsonrpc":"2.0", "error":{"message":"..."}, "id":1} ← failure
//
//   The "id" lets the Python client match responses to requests.
//   We return the same id that was sent.
//
// ─── THREADING MODEL ─────────────────────────────────────────────────────────
//   One io_context with ONE thread.  All WebSocket sessions are async (Boost.Beast
//   async_read/async_write), so multiple simultaneous connections are handled by
//   cooperative coroutines on a single OS thread.
//   No mutex needed around SharedDatabase reads — single-threaded io_context.
//
//   For production: set ioc thread count to hardware_concurrency() and add
//   std::mutex to SharedDatabase write operations.
// ─────────────────────────────────────────────────────────────────────────────
#include "eda_cli/eda_daemon.hpp"

#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>

#include <nlohmann/json.hpp>

// ── WebSocket transport (reused from routing_genetic_astar) ──────────────────
// This header defines WebSocketServer, Listener, WebSocketSession.
// ANALOGY: We're plugging a new "brain" (our dispatch logic) into an existing
// "phone switchboard" (WebSocket server).
#include "eda_router/transport/websocket_transport.hpp"

// ── Algorithm library headers ─────────────────────────────────────────────────
// SharedDatabase: the in-memory design database shared by all MCP servers.
#include "eda_router/shared_database.hpp"

// MCP server implementations (DB, Router; Placer is a stub for now).
#include "eda_router/mcp/servers/db_mcp_server.hpp"
#include "eda_router/mcp/servers/routing_mcp_server.hpp"
#include "eda_router/mcp/servers/placer_mcp_server.hpp"

using json = nlohmann::json;

namespace eda {

// ─── JSON-RPC response builder helpers ───────────────────────────────────────
// ANALOGY: Two stamp templates. result_response() stamps "SUCCESS — here's the
// result". error_response() stamps "FAILED — here's what went wrong."
// Both include the request id so the caller can match them.
static json result_response(const json& id, const json& result) {
    return {{"jsonrpc", "2.0"}, {"result", result}, {"id", id}};
}
static json error_response(const json& id, const std::string& msg, int code = -32603) {
    return {
        {"jsonrpc", "2.0"},
        {"error",   {{"code", code}, {"message", msg}}},
        {"id",      id}
    };
}

// ─── dispatch() — The Central Router ─────────────────────────────────────────
// WHAT IT DOES:
//   Reads one JSON-RPC request string and returns one JSON-RPC response string.
//   This is what we pass as the MessageHandler to WebSocketServer.
//   WebSocketServer calls this for EVERY message received from a client.
//
// STEP BY STEP:
//   1. Parse the incoming string as JSON             [json::parse]
//   2. Extract "method", "params", "id"              [value()]
//   3. Look up the method name in a big if-else chain
//   4. Call the appropriate MCP server method
//   5. Wrap the result in a JSON-RPC envelope and return as string
//
// ERROR PATHS:
//   - Invalid JSON          → JSON-RPC error -32700 "Parse error"
//   - Unknown method        → JSON-RPC error -32601 "Method not found"
//   - MCP server exception  → JSON-RPC error -32603 "Internal error"
//
// NOTE: db and routing are captured by shared_ptr — they outlive individual calls.
static std::string dispatch(
    const std::string& raw,
    std::shared_ptr<mcp::DbMcpServer>       db_server,
    std::shared_ptr<mcp::RoutingMcpServer>    router_server,
    std::shared_ptr<mcp::PlacerMcpServer>     placer_server
) {
    // ── Step 1: Parse ─────────────────────────────────────────────────────────
    json req;
    try {
        req = json::parse(raw);
    } catch (const json::parse_error& e) {
        // JSON-RPC parse error: id is unknown (null per spec)
        return error_response(nullptr, std::string("Parse error: ") + e.what(), -32700).dump();
    }

    const json id     = req.value("id",     nullptr);
    const auto method = req.value("method", std::string{});
    const auto params = req.value("params", json::object());

    std::cout << "[Daemon] → " << method << "  id=" << id.dump() << "\n";

    // ── Step 2: Dispatch ──────────────────────────────────────────────────────
    try {
        // ── Health check ──────────────────────────────────────────────────────
        if (method == "ping") {
            return result_response(id, "pong").dump();
        }

        // ── DB methods ────────────────────────────────────────────────────────
        if (method == "load_design")    return result_response(id, db_server->load_design(params)).dump();
        if (method == "db.status")      return result_response(id, db_server->status()).dump();
        if (method == "db.query_nets")  return result_response(id, db_server->query_nets()).dump();
        if (method == "db.query_cells") return result_response(id, db_server->query_cells()).dump();
        if (method == "db.query_bbox")  return result_response(id, db_server->query_bbox()).dump();
        if (method == "db.net_bbox")    return result_response(id, db_server->net_bbox(params)).dump();

        // ── Routing methods ───────────────────────────────────────────────────
        if (method == "route_nets")     return result_response(id, router_server->route_nets(params)).dump();
        if (method == "drc.check")      return result_response(id, router_server->check_drc()).dump();
        if (method == "eco.fix_drc")    return result_response(id, router_server->eco_fix_drc(params)).dump();

        if (method == "place_cells") {
            return result_response(id, placer_server->place_cells(params)).dump();
        }

        // ── Method not found ──────────────────────────────────────────────────
        return error_response(id, "Method not found: " + method, -32601).dump();

    } catch (const std::exception& e) {
        // Internal error — MCP server threw an exception.
        std::cerr << "[Daemon] Exception in " << method << ": " << e.what() << "\n";
        return error_response(id, std::string("Internal error: ") + e.what()).dump();
    }
}

// ─── Daemon implementation ────────────────────────────────────────────────────
Daemon::Daemon(std::string host, int port)
    : host_(std::move(host)), port_(port) {}

void Daemon::run() {
    // ── Create shared state ───────────────────────────────────────────────────
    // SharedDatabase is the single source of truth.
    // All MCP servers hold a shared_ptr to it — they read/write the same object.
    auto db = std::make_shared<SharedDatabase>();

    // ── Create MCP servers ────────────────────────────────────────────────────
    // ANALOGY: Hiring department heads, each given access to the company's
    // central filing cabinet (SharedDatabase).
    auto db_server      = std::make_shared<mcp::DbMcpServer>(db);
    auto router_server  = std::make_shared<mcp::RoutingMcpServer>(db);
    auto placer_server  = std::make_shared<mcp::PlacerMcpServer>(db);

    // ── Create the WebSocket server ───────────────────────────────────────────
    // WebSocketServer is from websocket_transport.hpp.
    // It handles: TCP accept loops, WebSocket handshakes, async read/write.
    // We only need to give it one callback: the dispatch function above.
    transport::WebSocketServer ws_server{host_, port_};

    // The MessageHandler captures db_server and router_server by shared_ptr.
    // ANALOGY: Giving the front desk a phone directory (captures) that lists
    // all department heads and their extensions.
    ws_server.set_message_handler([db_server, router_server, placer_server](const std::string& raw) {
        return dispatch(raw, db_server, router_server, placer_server);
    });

    std::cout << "[Daemon] All MCP servers initialized.\n";
    std::cout << "[Daemon] Ready.  Waiting for connections on ws://"
              << host_ << ":" << port_ << "\n\n";

    // Blocks until io_context is stopped (Ctrl+C / SIGTERM).
    ws_server.run();
}

} // namespace eda
