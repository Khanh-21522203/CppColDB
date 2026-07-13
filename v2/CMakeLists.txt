cmake_minimum_required(VERSION 3.20)
project(CppColDBv2 VERSION 2.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# CppColDB v2 — clean, abstractions-first, modular scaffold.
#
# This is a SCAFFOLD: declarations are complete, bodies throw
# CPPCOLDB_NOT_IMPLEMENTED(). It compiles and links; it runs nothing yet.
#
# Module dependency direction (one-directional, enforced by target_link_libraries):
#
#   common  (leaf)
#      ^  ^  ^
#      |  |  +----- infrastructure  (common + engine abstractions)
#      |  +-------- sql             (common + engine)
#      +----------- engine          (common)
#                      ^
#   main (public facade / composition root) -> common + engine + infrastructure + sql
#   cli  -> main
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CPPCOLDBV2_BUILD_TESTS "Build the v2 test tree" ON)
option(CPPCOLDBV2_BUILD_CLI   "Build the v2 CLI/REPL executable" ON)

# Modules, in dependency order.
add_subdirectory(src/common)
add_subdirectory(src/engine)
add_subdirectory(src/infrastructure)
add_subdirectory(src/sql)
add_subdirectory(src/main)

if(CPPCOLDBV2_BUILD_CLI)
    add_subdirectory(src/cli)
endif()

if(CPPCOLDBV2_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
