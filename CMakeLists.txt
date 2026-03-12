cmake_minimum_required(VERSION 3.20)
project(CppColDB VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Library — all src/**/*.cpp compiled into a static lib
# ---------------------------------------------------------------------------
file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
if(LIB_SOURCES)
    add_library(cppcoldb_lib STATIC ${LIB_SOURCES})
    target_include_directories(cppcoldb_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
else()
    # Allow bootstrapping the project before any implementation files exist.
    add_library(cppcoldb_lib INTERFACE)
    target_include_directories(cppcoldb_lib INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
endif()

# ---------------------------------------------------------------------------
# Main executable (SQL REPL)
# ---------------------------------------------------------------------------
add_executable(cppcoldb main.cpp)
target_link_libraries(cppcoldb PRIVATE cppcoldb_lib)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()

function(add_phase_tests name)
    # Remaining args are source files
    set(sources ${ARGN})
    add_executable(${name} ${sources})
    target_link_libraries(${name} PRIVATE cppcoldb_lib)
    target_include_directories(${name} PRIVATE src/)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Uncomment each block as you reach the corresponding phase.

# Phase 0 — Core types & error handling
# add_phase_tests(test_phase0
#     test/common/test_types.cpp
#     test/common/test_data_chunk.cpp
#     test/common/test_error_handling.cpp
# )

# Phase 1 — SQL parser
# add_phase_tests(test_phase1
#     test/parser/test_tokenizer.cpp
#     test/parser/test_parser.cpp
# )

# Phase 2 — Storage foundation
# add_phase_tests(test_phase2
#     test/storage/test_block_file.cpp
#     test/storage/test_buffer_manager.cpp
#     test/storage/test_wal.cpp
#     test/storage/test_compression.cpp
# )

# Phase 3 — Column storage
# add_phase_tests(test_phase3
#     test/storage/test_column_chunk.cpp
#     test/storage/test_row_group.cpp
# )

# Phase 4 — Catalog + Transaction
# add_phase_tests(test_phase4
#     test/catalog/test_catalog.cpp
#     test/transaction/test_transaction.cpp
#     test/transaction/test_mvcc.cpp
# )

# Phase 5 — Execution foundation
# add_phase_tests(test_phase5
#     test/execution/test_pipeline_executor.cpp
# )

# Phase 6 — Basic operators
# add_phase_tests(test_phase6
#     test/execution/test_table_scan.cpp
#     test/execution/test_filter.cpp
#     test/execution/test_projection.cpp
# )

# Phase 7 — Query planner
# add_phase_tests(test_phase7
#     test/planner/test_binder.cpp
#     test/planner/test_optimizer.cpp
#     test/planner/test_physical_planner.cpp
# )

# Phase 8 — Advanced operators
# add_phase_tests(test_phase8
#     test/execution/test_aggregation.cpp
#     test/execution/test_join.cpp
# )

# Phase 9 — Database bootstrap
# add_phase_tests(test_phase9
#     test/main/test_database.cpp
#     test/integration/test_end_to_end.cpp
# )

# Phase 10 — Query profiler
# add_phase_tests(test_phase10
#     test/profiler/test_query_profiler.cpp
# )
