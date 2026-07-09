cmake_minimum_required(VERSION 3.16)
project(starboard LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Strict Arithmetic Profile (docs/DESIGN.md §3.5): no FMA contraction, no
# fast-math. Cross-implementation bit-agreement on the verdict path is the
# product; -ffp-contract=off is mandatory for conformant builds.
add_compile_options(-Wall -Wextra -Wpedantic -ffp-contract=off)

# Reference implementation: header-only, dependency-free, allocation-free.
add_library(starboard_core INTERFACE)
target_include_directories(starboard_core INTERFACE core/include)

# Demo: two rival-operator satellites de-conflict a crossing, then a
# three-body window with an uncooperative derelict.
add_executable(starboard_crossing_demo examples/crossing_demo.cpp)
target_link_libraries(starboard_crossing_demo PRIVATE starboard_core)

# Conformance suite (docs/CONFORMANCE.md) — the certification product.
enable_testing()
add_executable(starboard_conformance conformance/runner.cpp)
target_link_libraries(starboard_conformance PRIVATE starboard_core)
add_test(NAME conformance COMMAND starboard_conformance)
