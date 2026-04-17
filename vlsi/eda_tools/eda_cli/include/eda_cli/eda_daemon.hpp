// ═══════════════════════════════════════════════════════════════════════════════
// FILE: eda_cli/include/eda_cli/eda_daemon.hpp
// PURPOSE: Declaration of the EDA Daemon class
// ═══════════════════════════════════════════════════════════════════════════════
//
// WHAT THIS IS:
//   The top-level orchestrator that:
//     1. Owns the SharedDatabase (one instance, shared by all MCP servers)
//     2. Creates all MCP server instances, passing them shared_ptr<SharedDatabase>
//     3. Starts the WebSocket server (using the existing WebSocketServer from
//        routing_genetic_astar/transport/websocket_transport.hpp)
//     4. Routes incoming JSON-RPC method calls to the correct MCP server
//
// ANALOGY: A hotel front desk.
//   The front desk (eda_daemon) is the single point of contact for all guests
//   (Python agent requests).  When a guest asks for room service (route_nets),
//   the desk calls the kitchen (RoutingMcpServer) and returns the result.
//   The guest never calls the kitchen directly.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once
#include <string>
#include <memory>

// Forward declaration — implementation is in eda_daemon.cpp.
// This keeps this header fast to include (no heavy Boost.Beast headers here).
namespace eda { class Daemon; }

namespace eda {

class Daemon {
public:
    // Constructs the daemon with bind address and port.
    // Does NOT start the server yet — call run() to start listening.
    explicit Daemon(std::string host, int port);

    // Starts the WebSocket server.  BLOCKS until SIGINT/SIGTERM.
    void run();

    // NOT copyable — the daemon owns unique resources (io_context, sockets).
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

private:
    std::string host_;
    int         port_;
};

} // namespace eda
