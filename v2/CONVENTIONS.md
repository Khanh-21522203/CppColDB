# CppColDBv2 Scaffold Conventions (single source of truth)

Every module-scaffolding agent MUST follow this document exactly so the independently
generated modules link together. Read it fully before writing any file.

This is a **scaffold**: every declaration is complete, every non-trivial body throws
`CPPCOLDB_NOT_IMPLEMENTED()`. The tree must compile and link. It runs nothing.

## 1. Directory layout (per module)

Each module lives under `v2/src/<module>/` and has this shape:

```
v2/src/<module>/
  CMakeLists.txt
  include/cppcoldb/<module>/<domain>/<name>.hpp      # public headers
  src/<domain>/<name>.cpp                              # stub bodies
```

- Public headers go under `include/cppcoldb/<module>/...` so includes are always
  `#include "cppcoldb/<module>/<domain>/<name>.hpp"` (angle-or-quote: use quotes).
- Implementation `.cpp` files mirror the header path under `src/` (minus the
  `include/cppcoldb/<module>` prefix). A header at
  `include/cppcoldb/engine/storage/buffer_manager.hpp` has its stub at
  `src/storage/buffer_manager.cpp`.

## 2. Naming

- Files: `snake_case.hpp` / `snake_case.cpp`, one primary type per file (a tight family
  such as a build+probe operator pair may share a file, as in v1).
- Types (classes/structs/enums): `PascalCase`. Enum members: `SCREAMING_SNAKE_CASE`.
- Interfaces (pure-virtual abstract base classes): prefixed `I`, e.g. `IBufferManager`,
  file `i_buffer_manager.hpp`, living under `.../abstractions/<domain>/`.
- Members/locals: `snake_case`; private data members get a trailing underscore (`bm_`).
- Constants: `SCREAMING_SNAKE_CASE`. Header guard: `#pragma once` (never macro guards).

## 3. Namespaces (nested — this is the key v1→v2 fix)

- `common`      -> `namespace cppcoldb::common`
- `engine`      -> `namespace cppcoldb::engine::<domain>` where `<domain>` is
                   `storage`, `catalog`, `transaction`, `checkpoint`, `execution`,
                   `scheduler`, `profiler`. Abstractions use the SAME domain namespace
                   as their implementations (e.g. `IBufferManager` is in
                   `cppcoldb::engine::storage`, next to `BufferManager`).
                   Cross-cutting engine I/O ports (`abstractions/io/`) use
                   `cppcoldb::engine::io`.
- `infrastructure` -> `namespace cppcoldb::infrastructure::<domain>` (`io`, `time`, `logging`).
- `sql`         -> `namespace cppcoldb::sql::parser` and `cppcoldb::sql::planner`.
- `main`        -> `namespace cppcoldb` (the public facade lives in the root namespace).

## 4. The stub macro

`common` provides `cppcoldb/common/not_implemented.hpp`:

```cpp
#include "cppcoldb/common/not_implemented.hpp"
// ...
ReturnType Foo::Bar() { CPPCOLDB_NOT_IMPLEMENTED(); }
```

Rules for stub bodies in `.cpp`:
- A function that returns `void` or any type: body is exactly `CPPCOLDB_NOT_IMPLEMENTED();`
  (the macro is `[[noreturn]]`, so no `return` is needed and the compiler is satisfied).
- Constructors/destructors: default them in the header (`= default`) where possible.
  If a constructor must appear in the `.cpp`, give it an empty body `{}` (do NOT throw
  from a constructor).
- Pure virtual interface methods: declared `= 0` in the `I*.hpp` header, NO `.cpp`.
- Trivial inline accessors and strong-type structs: fully defined in the header (no stub).

## 5. Abstractions-first (interfaces)

- Every core engine subsystem gets a pure-virtual interface under
  `include/cppcoldb/engine/abstractions/<domain>/i_<name>.hpp`.
- An interface: virtual destructor `= default`, all methods pure `= 0`, includes only
  `common` types and other abstractions — NEVER a concrete implementation header.
- The concrete class `#include`s its own interface and inherits it:
  `class BufferManager final : public IBufferManager { ... };`
- Interfaces to author (minimum set):
  - storage: `IBlockManager`, `IBufferManager`, `IBufferHandle`, `IWal`
  - catalog: `ICatalog`, `ISchemaStore`
  - transaction: `ITransaction`, `ITransactionManager`, `IVersionManager`
  - checkpoint: `ICheckpointManager`, `IRecoveryManager`
  - compression: `ICompressionCodec`
  - scheduler: `ITask`, `ITaskScheduler`
  - io (ports for infrastructure): `IFileSystem`, `IClock`, `ILogger`

## 6. CMake per module (copy this pattern exactly)

```cmake
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
if(SOURCES)
    add_library(cppcoldbv2_<module> STATIC ${SOURCES})
else()
    add_library(cppcoldbv2_<module> INTERFACE)
endif()

if(SOURCES)
    target_include_directories(cppcoldbv2_<module> PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_features(cppcoldbv2_<module> PUBLIC cxx_std_20)
    target_link_libraries(cppcoldbv2_<module> PUBLIC <deps...>)
else()
    target_include_directories(cppcoldbv2_<module> INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(cppcoldbv2_<module> INTERFACE <deps...>)
endif()
```

Target names and their link deps (PUBLIC unless noted):
- `cppcoldbv2_common`         : (no deps)
- `cppcoldbv2_engine`         : cppcoldbv2_common
- `cppcoldbv2_infrastructure` : cppcoldbv2_common cppcoldbv2_engine
- `cppcoldbv2_sql`            : cppcoldbv2_common cppcoldbv2_engine
- `cppcoldbv2` (main)         : cppcoldbv2_common cppcoldbv2_engine cppcoldbv2_infrastructure cppcoldbv2_sql
- `cppcoldbv2_cli` (exe)      : PRIVATE cppcoldbv2
- test targets                : PRIVATE the module under test (+ engine for fakes)

Every module WILL have at least one `.cpp` stub, so the STATIC branch is the real path;
keep the INTERFACE fallback only for bootstrap safety.

## 7. What `common` provides (already built — depend on it, do not redefine)

Headers under `include/cppcoldb/common/`:
- `not_implemented.hpp`  -> `CPPCOLDB_NOT_IMPLEMENTED()`, `NotImplementedError`
- `exception.hpp`        -> `CppColDBException`, `ParseError`, `BindError`, `RuntimeError`, `IOError`
- `constants.hpp`        -> `STANDARD_VECTOR_SIZE`, `ROW_GROUP_SIZE`, `DEFAULT_BLOCK_SIZE`
- `types/`               -> strong-typed IDs & value objects (one per file):
  `block_id.hpp` (BlockId), `transaction_id.hpp` (TransactionId),
  `row_group_id.hpp` (RowGroupId), `row_id.hpp` (RowId + Make/Group/Offset helpers),
  `column_id.hpp` (ColumnId), `table_id.hpp` (TableId), `timestamp.hpp` (Timestamp),
  `type_id.hpp` (TypeId enum + Type* helpers), `value.hpp` (Value),
  `data_chunk.hpp` (DataVector, DataChunk), `isolation_level.hpp` (IsolationLevel)
- `primitives/`          -> domain-agnostic utilities (header-only):
  `cache_line_padded.hpp`, `spinlock.hpp`, `object_pool.hpp`,
  `arena_allocator.hpp`, `concurrent_queue.hpp`, `bit_utils.hpp`

All of the above are in `namespace cppcoldb::common`. Refer to them fully qualified or
with a `namespace cc = cppcoldb::common;` alias inside your module.

## 8. Module ownership map (disjoint — do not write outside your subtree)

- Agent 1: `engine/` storage + checkpoint + scheduler  (+ their abstractions)
- Agent 2: `engine/` catalog + transaction              (+ their abstractions)
- Agent 3: `engine/` execution + profiler               (+ their abstractions)
- Agent 4: `sql/` parser + planner
- Agent 5: `infrastructure/` io + time + logging
- Agent 6: `main/` (facade) + `cli/`
- Agent 7: `test/` tree + `design/` HTML + `README.md`

engine/ is split across agents 1–3; each writes only its own domain subfolders and its
own `abstractions/<domain>/` files. The `engine/CMakeLists.txt` is owned by Agent 1 and
globs the whole `engine/src` tree, so agents 2 and 3 only add headers + `.cpp` stubs.
