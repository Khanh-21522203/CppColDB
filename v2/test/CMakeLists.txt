# ---------------------------------------------------------------------------
# CppColDBv2 test tree.
#
# Mirrors src/ 1:1 (see CONVENTIONS.md section 1 and design/v2-scaffold-map.md).
# One test executable per module + integration + a benchmark executable (the
# benchmark is intentionally NOT registered with add_test()). Fakes for the
# engine abstractions live under helpers/ and are #include-d directly by the
# test/engine/**/*.cpp stub that exercises the corresponding interface, which
# is what forces them to compile as part of test_engine.
#
# This is a SCAFFOLD: every stub file is a placeholder. It compiles, links,
# and each ctest target runs its (empty) main() to a clean exit; it asserts
# nothing yet.
# ---------------------------------------------------------------------------

function(add_cppcoldbv2_test name)
    # Remaining args are this target's source files.
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# --- common ------------------------------------------------------------------
file(GLOB_RECURSE TEST_COMMON_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/common/*.cpp")
add_cppcoldbv2_test(test_common ${TEST_COMMON_SOURCES})
target_link_libraries(test_common PRIVATE cppcoldbv2_common)

# --- engine (+ helpers/ fakes, included directly by the stubs below) --------
file(GLOB_RECURSE TEST_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/*.cpp")
add_cppcoldbv2_test(test_engine ${TEST_ENGINE_SOURCES})
target_link_libraries(test_engine PRIVATE cppcoldbv2_engine cppcoldbv2_common)

# --- sql -----------------------------------------------------------------
file(GLOB_RECURSE TEST_SQL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/sql/*.cpp")
add_cppcoldbv2_test(test_sql ${TEST_SQL_SOURCES})
target_link_libraries(test_sql PRIVATE cppcoldbv2_sql cppcoldbv2_engine cppcoldbv2_common)

# --- main (facade) -----------------------------------------------------------
file(GLOB_RECURSE TEST_MAIN_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/main/*.cpp")
add_cppcoldbv2_test(test_main ${TEST_MAIN_SOURCES})
target_link_libraries(test_main PRIVATE cppcoldbv2)

# --- integration ---------------------------------------------------------
file(GLOB_RECURSE TEST_INTEGRATION_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/integration/*.cpp")
add_cppcoldbv2_test(test_integration ${TEST_INTEGRATION_SOURCES})
target_link_libraries(test_integration PRIVATE cppcoldbv2)

# --- benchmark (NOT a ctest target) -----------------------------------------
add_executable(benchmark_cppcoldbv2 "${CMAKE_CURRENT_SOURCE_DIR}/benchmark/benchmark_main.cpp")
target_link_libraries(benchmark_cppcoldbv2 PRIVATE cppcoldbv2)
