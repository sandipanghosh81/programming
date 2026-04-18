cmake_minimum_required(VERSION 3.24)
project(eda_router VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# ── nlohmann JSON (MCP / JSON-RPC serialisation) ─────────────────────────────
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# ── Boost (graph, container, pool, beast, system) ────────────────────────────
# boost::adjacency_list lives in Boost::graph (header-only for vecS/vecS builds).
# boost::container::flat_map lives in Boost::container.
# boost::object_pool lives in Boost::pool.
# All three are header-only — Boost::headers target covers them.
find_package(Boost 1.80.0 REQUIRED)

# ── Shared implementation units (DEF I/O + GridFill + OASIS/Delta writers) ────
add_library(routing_genetic_astar_io STATIC
    src/io/def_design_loader.cpp
    src/planner/grid_fill.cpp
    src/oasis_writer.cpp           # [planned] OASIS streaming encoder
    src/binary_delta_writer.cpp    # [planned] compact delta format for HEA Apply EU
    src/via_expander.cpp           # [planned] two-stage via geometry expander
)
target_include_directories(routing_genetic_astar_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(routing_genetic_astar_io PUBLIC nlohmann_json::nlohmann_json Boost::headers)
target_compile_features(routing_genetic_astar_io PUBLIC cxx_std_23)

# Renamed target alias (preferred by downstream projects).
add_library(eda_router_io ALIAS routing_genetic_astar_io)

# ── INTERFACE libraries: enforce 13-namespace dependency DAG at compile time ──
# Each INTERFACE target restricts include paths to its own namespace only.
# Violating the DAG produces a compile error rather than a linker surprise.
foreach(mod core analysis threading planner routing constraints convergence eco evaluation)
    add_library(rga_${mod} INTERFACE)
    target_include_directories(rga_${mod} INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/routing_genetic_astar/${mod}>
    )
endforeach()

# Encode the dependency DAG edges
target_link_libraries(rga_analysis    INTERFACE rga_core)
target_link_libraries(rga_threading   INTERFACE rga_core)
target_link_libraries(rga_planner     INTERFACE rga_core rga_analysis)
target_link_libraries(rga_routing     INTERFACE rga_core rga_analysis rga_planner rga_threading)
target_link_libraries(rga_constraints INTERFACE rga_core rga_analysis rga_routing)
target_link_libraries(rga_convergence INTERFACE rga_core rga_routing rga_threading)
target_link_libraries(rga_eco         INTERFACE rga_core rga_routing rga_threading)
target_link_libraries(rga_evaluation  INTERFACE rga_core rga_routing rga_constraints)

# ── OR-Tools (optional — ILP fallback solver) ─────────────────────────────────
find_package(ortools QUIET)

# ── pybind11 (optional — Optuna offline meta-tuner) ──────────────────────────
find_package(pybind11 QUIET)

add_executable(vlsi_daemon
    src/vlsi_daemon.cpp
    src/routing_pipeline.cpp
)

target_include_directories(vlsi_daemon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(vlsi_daemon PRIVATE
    nlohmann_json::nlohmann_json
    Boost::headers
    routing_genetic_astar_io
)

target_compile_definitions(vlsi_daemon PRIVATE
    BOOST_BEAST_USE_STD_STRING_VIEW
)

# Enable OR-Tools integration if found.
if(ortools_FOUND)
    target_link_libraries(vlsi_daemon PRIVATE ortools::ortools)
    target_compile_definitions(vlsi_daemon PRIVATE HAVE_OR_TOOLS)
    message(STATUS "OR-Tools found — ILP solver enabled")
else()
    message(STATUS "OR-Tools NOT found — ILP solver will use noop fallback")
endif()

# Enable pybind11 integration if found.
if(pybind11_FOUND)
    target_link_libraries(vlsi_daemon PRIVATE pybind11::embed)
    target_compile_definitions(vlsi_daemon PRIVATE HAVE_PYBIND11)
    message(STATUS "pybind11 found — Optuna tuner enabled")
else()
    message(STATUS "pybind11 NOT found — OptunaTuner will use identity fallback")
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# CTest — Unit Tests
# ═══════════════════════════════════════════════════════════════════════════════
# Enable CTest so `ctest --test-dir build` runs all registered tests.
# Each test is a standalone executable that returns 0 on success, 1 on failure.
enable_testing()

# Helper function: compile one test file into a test executable and register it.
#   name   = short test name identifier (binary name + CTest test name)
#   source = path to the .cpp test source file
function(add_vlsi_test name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(${name} PRIVATE
        nlohmann_json::nlohmann_json
        Boost::headers
    )
    target_compile_definitions(${name} PRIVATE
        BOOST_BEAST_USE_STD_STRING_VIEW
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# ── Register all test targets ─────────────────────────────────────────────────
add_vlsi_test(test_grid_graph           tests/test_grid_graph.cpp)
add_vlsi_test(test_design_analyzer      tests/test_design_analyzer.cpp)
add_vlsi_test(test_global_planner       tests/test_global_planner.cpp)
add_vlsi_test(test_detailed_grid_router tests/test_detailed_grid_router.cpp)
add_vlsi_test(test_convergence_monitor  tests/test_convergence_monitor.cpp)
add_vlsi_test(test_strategy_composer    tests/test_strategy_composer.cpp)
