# CppColDBv2

CppColDBv2 is a scaffold of an abstractions-first, modular rewrite of CppColDB, a
DuckDB-inspired embedded columnar OLAP engine. It is modeled on the SharpDB V2 rewrite:
the same module split, the same ports/adapters pattern, and the same
implement-bottom-up discipline.

Note: this is a scaffold, not a working engine. Every module compiles and links, but
non-trivial method bodies throw `NotImplementedError` via the `CPPCOLDB_NOT_IMPLEMENTED()`
macro (defined in `src/common/include/cppcoldb/common/not_implemented.hpp`). Declarations
are complete; behavior is not yet implemented. The original v1 implementation under the
repository's top-level `src/` directory is untouched and continues to run independently
of this scaffold.

## What this is

CppColDBv2 restructures CppColDB around six independent CMake targets with a strictly
one-directional dependency graph, nested namespaces per module and domain, and a
pure-virtual interface layer (`engine/abstractions/`) that concrete implementations sit
behind. The goal is a codebase where storage, catalog, transactions, checkpointing,
execution, SQL, and the public facade are each independently buildable and testable, with
fakes able to stand in for any engine subsystem in tests.

## Module and layer breakdown

```text
common  (leaf: types/, primitives/, exceptions, not_implemented macro)
   ^
   +-- engine          (abstractions/, storage/, catalog/, transaction/, checkpoint/,
   |                     execution/, scheduler/, profiler/)
   +-- infrastructure  (io/, time/, logging/ — adapters implementing engine's io ports)
   +-- sql             (parser/, planner/ — emits an engine operator tree)
              ^
              +-- main  (Database, Connection, ClientContext — public facade,
              |          composition root)
              +-- cli   (REPL executable)
```

| Module           | CMake target                | Purpose |
|------------------|-------------------------------|---------|
| `common`         | `cppcoldbv2_common`            | Strong-typed IDs and value types, concurrency/allocation primitives, exception hierarchy. No dependencies. |
| `engine`         | `cppcoldbv2_engine`             | The database core: interfaces, columnar storage, MVCC transactions, checkpoint/recovery, the vectorized execution runtime, profiler, and scheduler. |
| `infrastructure` | `cppcoldbv2_infrastructure`     | Concrete adapters (filesystem, clock, logging) implementing engine's I/O ports. |
| `sql`            | `cppcoldbv2_sql`                 | SQL tokenizer/parser/AST, binder, optimizer, and physical planner. Emits an engine operator tree rather than executing it directly. |
| `main`           | `cppcoldbv2`                     | Public embeddable API facade and composition root that wires engine, infrastructure, and sql together. |
| `cli`            | `cppcoldbv2_cli`                 | Thin REPL executable depending only on `main`. |

For the full directory tree, the dependency graph in text form, and the interface
inventory, see `design/v2-scaffold-map.md`. For an interactive, clickable module map with
per-module notes and the implementation-order build sequence, open
`design/v2-architecture.html` directly in a browser — it is self-contained and requires
no network access or build step.

## Dependency rules

The dependency graph is enforced one-directionally and must not form cycles:

- `common` has no dependencies. It is the only leaf module.
- `engine` depends only on `common`.
- `infrastructure` depends on `common` and `engine` (it implements engine's abstract I/O
  ports), but never on `sql`.
- `sql` depends on `common` and `engine` (it emits an engine operator tree), but never on
  `infrastructure`.
- `main` is the composition root and is the only module permitted to depend on all four
  of `common`, `engine`, `infrastructure`, and `sql`.
- `cli` depends only on `main`, privately. It must not include engine, sql, or
  infrastructure headers directly.
- Test targets depend on the module under test, privately, plus `cppcoldbv2_engine` when
  a test needs fakes for engine abstractions.

See `CONVENTIONS.md` for the complete rationale, naming conventions, and the CMake
pattern every module follows.

## Build

Requirements:

- CMake 3.20 or later
- A C++20 compiler (GCC, Clang, or MSVC)

Commands:

```bash
cmake -S v2 -B v2/build
cmake --build v2/build
```

## Test

```bash
ctest --test-dir v2/build --output-on-failure
```

The test tree under `v2/test/` mirrors the source tree under `v2/src/` module for
module. Because every non-trivial body currently throws `NotImplementedError`, most test
stubs are expected to assert that behavior rather than exercise real functionality until
each module's implementation phase begins.

## Contributing

Read `CONVENTIONS.md` before adding to any module. It is the single source of truth for
directory layout, naming, namespace rules, the stub macro, the abstractions-first
interface pattern, the CMake target pattern, and the module ownership map. All bodies in
this tree currently throw `NotImplementedError` by design — do not replace a stub with
real logic without also updating its corresponding test and, if the module's public
shape changes, `design/v2-scaffold-map.md` and `design/v2-architecture.html`.

This README, like the rest of the design documentation in `v2/design/`, is a draft and
subject to revision as implementation proceeds.
