cmake_minimum_required(VERSION 3.24)
project(eda_cli VERSION 1.0.0 LANGUAGES CXX)

# ─── C++23 (same standard as routing_genetic_astar) ──────────────────────────
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# ─── nlohmann JSON (JSON-RPC serialisation) ───────────────────────────────────
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# ─── Boost (headers for websockets beast + graph usage through router lib) ────
find_package(Boost 1.80.0 REQUIRED)

# ─── eda_router (the routing algorithm library) ───────────────────────────────
# This library is built separately in ../eda_router.
# For development: use add_subdirectory; for installation: use find_package.
#
# WHY SUBDIRECTORY INSTEAD OF FIND_PACKAGE?
#   In a monorepo where both projects live under cpp_programs/, add_subdirectory
#   is simpler — it compiles both together with shared CMake state.
#   In a production build system (e.g., out-of-tree install), switch to
#   find_package(eda_router REQUIRED).
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../eda_router
    ${CMAKE_CURRENT_BINARY_DIR}/eda_router
)

add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../eda_placer
    ${CMAKE_CURRENT_BINARY_DIR}/eda_placer
)

# ─── eda_daemon executable ────────────────────────────────────────────────────
add_executable(eda_daemon
    src/main.cpp
    src/eda_daemon.cpp
)

target_include_directories(eda_daemon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../eda_router/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../eda_placer/include
)

target_link_libraries(eda_daemon PRIVATE
    nlohmann_json::nlohmann_json
    Boost::headers
    eda_router_io
    eda_placer
)

target_compile_definitions(eda_daemon PRIVATE
    BOOST_BEAST_USE_STD_STRING_VIEW
)
