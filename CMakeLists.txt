cmake_minimum_required(VERSION 3.16)
project(starboard LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wpedantic)

# Reference implementation core (header + one translation unit).
add_library(starboard_core STATIC core/src/rules.cpp)
target_include_directories(starboard_core PUBLIC core/include)

# Demo: two rival-operator satellites de-conflict a crossing.
add_executable(starboard_crossing_demo examples/crossing_demo.cpp)
target_link_libraries(starboard_crossing_demo PRIVATE starboard_core)
