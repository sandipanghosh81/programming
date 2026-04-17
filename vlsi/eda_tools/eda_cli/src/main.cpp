// ═══════════════════════════════════════════════════════════════════════════════
// FILE: eda_cli/src/main.cpp  —  Entry Point for the EDA CLI Daemon
// ═══════════════════════════════════════════════════════════════════════════════
//
// WHAT THIS DOES:
//   Parses command-line arguments and starts the WebSocket MCP daemon.
//   This is the only file that has a main() — all other .cpp files are libraries.
//
// USAGE:
//   ./eda_daemon                    # Default: port 8080
//   ./eda_daemon --port 9090        # Custom port
//   ./eda_daemon --help             # Print usage
//
// HOW THIS DIFFERS FROM routing_genetic_astar/src/vlsi_daemon.cpp:
//   routing_genetic_astar's daemon is now REMOVED (deprecated).
//   This file is the NEW standalone gateway.
//   routing_genetic_astar provides only the routing LIBRARY.
//   eda_cli provides the NETWORK I/O.  They are separate concerns.
// ─────────────────────────────────────────────────────────────────────────────
#include <iostream>
#include <string>
#include <stdexcept>

// The daemon orchestrates all MCP servers and the WebSocket server.
// (Implementation in eda_daemon.cpp)
#include "eda_cli/eda_daemon.hpp"

// ─── print_usage ──────────────────────────────────────────────────────────────
static void print_usage(const char* prog) {
    std::cout <<
        "Usage: " << prog << " [OPTIONS]\n"
        "\n"
        "Options:\n"
        "  --port <PORT>   WebSocket port to listen on (default: 8080)\n"
        "  --help          Show this help message\n"
        "\n"
        "The daemon exposes the following JSON-RPC methods over WebSocket:\n"
        "  ping, load_design, db.status, db.query_nets, db.query_cells,\n"
        "  db.query_bbox, db.net_bbox, route_nets, drc.check, eco.fix_drc,\n"
        "  place_cells, window.current_view\n"
        "\n"
        "See eda_cli/README.md for the complete method reference.\n";
}

int main(int argc, char* argv[]) {
    int port = 8080;

    for (int i = 1; i < argc; ++i) {
        std::string arg{argv[i]};
        if (arg == "--help" || arg == "-h") { print_usage(argv[0]); return 0; }
        if (arg == "--port" && i + 1 < argc) { port = std::stoi(argv[++i]); continue; }
        std::cerr << "Unknown argument: " << arg << "\n";
        print_usage(argv[0]);
        return 1;
    }

    std::cout << "╔══════════════════════════════════════════════════════╗\n"
              << "║  EDA CLI Daemon v1.0  (C++23)                       ║\n"
              << "╠══════════════════════════════════════════════════════╣\n"
              << "║  WebSocket MCP gateway: ws://127.0.0.1:" << port << "         ║\n"
              << "║  Python agent connects to this port.                ║\n"
              << "║  Press Ctrl+C to stop.                              ║\n"
              << "╚══════════════════════════════════════════════════════╝\n\n";

    eda::Daemon daemon{"127.0.0.1", port};
    daemon.run();  // Blocks until SIGINT/SIGTERM
    return 0;
}
