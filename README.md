# CppColDB

CppColDB is a C++20 learning project for building a column-oriented relational database from scratch.

Current state: repository scaffold and architecture/planning docs are in place, with a minimal executable entry point.

## Goals

- Build a columnar storage engine
- Implement a SQL parser, planner, optimizer, and vectorized executor
- Add transaction support (MVCC), WAL, checkpointing, and profiling

## Project Layout

```text
.
├── CMakeLists.txt
├── main.cpp
├── src/                 # Database engine source tree (feature modules)
├── test/                # Unit/integration test tree
├── plans/               # Implementation plans by subsystem
├── flows/               # Mermaid architecture and flow diagrams
└── tasks/               # Working task notes
```

## Prerequisites

- CMake 3.20+
- C++20 compiler (GCC/Clang/MSVC with C++20 support)
- A build tool supported by CMake (Ninja or Make)

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

Notes:
- `cppcoldb_lib` is built from `src/*.cpp` recursively when source files exist.
- If `src/` has no `.cpp` yet, CMake falls back to an interface target so configure/build still succeed.

## Run

```bash
./build/cppcoldb
```

Current output:

```text
Hello, World!
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

The phase test targets in `CMakeLists.txt` are currently commented out. As you implement each phase, uncomment the corresponding `add_phase_tests(...)` block.

## Roadmap and Design Docs

- Implementation order and dependencies: `plans/plan-implementation-guide.md`
- Component plans: `plans/`
- Architecture/flow diagrams: `flows/README.md`

## Development Notes

- Keep changes small and phase-focused.
- Add tests for each subsystem as it is implemented.
- Reconfigure after adding/removing source files:

```bash
cmake -S . -B build
```
