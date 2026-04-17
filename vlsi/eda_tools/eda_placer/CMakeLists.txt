cmake_minimum_required(VERSION 3.24)
project(eda_placer LANGUAGES CXX)

include(FetchContent)

# Header-only nlohmann::json (shared with other modules).
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(eda_placer STATIC
    src/analytical_placer.cpp
    src/analog_placer.cpp
)
target_include_directories(eda_placer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../eda_router/include
)
target_link_libraries(eda_placer PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(eda_placer PUBLIC cxx_std_23)

# Basic unit test (runs under top-level ctest when built via eda_cli).
enable_testing()
add_executable(test_analog_placer tests/test_analog_placer.cpp)
target_link_libraries(test_analog_placer PRIVATE eda_placer)
add_test(NAME test_analog_placer COMMAND test_analog_placer)
