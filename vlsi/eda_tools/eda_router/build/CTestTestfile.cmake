# CMake generated Testfile for 
# Source directory: /Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar
# Build directory: /Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[test_grid_graph]=] "/Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar/build/test_grid_graph")
set_tests_properties([=[test_grid_graph]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar/CMakeLists.txt;90;add_test;/Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar/CMakeLists.txt;94;add_vlsi_test;/Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar/CMakeLists.txt;0;")
add_test([=[test_design_analyzer]=] "/Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar/build/test_design_analyzer")
set_tests_properties([=[test_design_analyzer]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar/CMakeLists.txt;90;add_test;/Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar/CMakeLists.txt;95;add_vlsi_test;/Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar/CMakeLists.txt;0;")
add_test([=[test_global_planner]=] "/Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar/build/test_global_planner")
set_tests_properties([=[test_global_planner]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar/CMakeLists.txt;90;add_test;/Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar/CMakeLists.txt;96;add_vlsi_test;/Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar/CMakeLists.txt;0;")
add_test([=[test_detailed_grid_router]=] "/Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar/build/test_detailed_grid_router")
set_tests_properties([=[test_detailed_grid_router]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar/CMakeLists.txt;90;add_test;/Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar/CMakeLists.txt;97;add_vlsi_test;/Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar/CMakeLists.txt;0;")
add_test([=[test_convergence_monitor]=] "/Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar/build/test_convergence_monitor")
set_tests_properties([=[test_convergence_monitor]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar/CMakeLists.txt;90;add_test;/Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar/CMakeLists.txt;98;add_vlsi_test;/Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar/CMakeLists.txt;0;")
add_test([=[test_strategy_composer]=] "/Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar/build/test_strategy_composer")
set_tests_properties([=[test_strategy_composer]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar/CMakeLists.txt;90;add_test;/Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar/CMakeLists.txt;99;add_vlsi_test;/Users/sandipanghosh/programming/cpp_programs/routing_genetic_astar/CMakeLists.txt;0;")
subdirs("_deps/nlohmann_json-build")
