# CppColDBv2 Scaffold Map

This document is the static, text-form counterpart to `design/v2-architecture.html`. It
lists the directory tree, the dependency graph, the module-to-target table, and the
interface inventory for the CppColDBv2 scaffold. See `CONVENTIONS.md` for the rules that
generated this layout.

Status: scaffold only. Declarations are complete; non-trivial bodies throw
`CPPCOLDB_NOT_IMPLEMENTED()`. The tree is designed to compile and link; it runs nothing.

## 1. Directory tree

```text
v2/
├── CMakeLists.txt
├── CONVENTIONS.md
├── README.md
├── design/
│   ├── v2-architecture.html
│   └── v2-scaffold-map.md
├── src/
│   ├── common/
│   │   ├── CMakeLists.txt
│   │   ├── include/cppcoldb/common/
│   │   │   ├── constants.hpp
│   │   │   ├── exception.hpp
│   │   │   ├── not_implemented.hpp
│   │   │   ├── primitives/
│   │   │   │   ├── arena_allocator.hpp
│   │   │   │   ├── bit_utils.hpp
│   │   │   │   ├── cache_line_padded.hpp
│   │   │   │   ├── concurrent_queue.hpp
│   │   │   │   ├── object_pool.hpp
│   │   │   │   └── spinlock.hpp
│   │   │   └── types/
│   │   │       ├── block_id.hpp
│   │   │       ├── column_id.hpp
│   │   │       ├── data_chunk.hpp
│   │   │       ├── isolation_level.hpp
│   │   │       ├── row_group_id.hpp
│   │   │       ├── row_id.hpp
│   │   │       ├── table_id.hpp
│   │   │       ├── timestamp.hpp
│   │   │       ├── transaction_id.hpp
│   │   │       ├── type_id.hpp
│   │   │       └── value.hpp
│   │   └── src/
│   │       ├── data_chunk.cpp
│   │       ├── type_id.cpp
│   │       └── value.cpp
│   ├── engine/
│   │   ├── CMakeLists.txt
│   │   ├── include/cppcoldb/engine/
│   │   │   ├── abstractions/
│   │   │   │   ├── catalog/         (i_catalog.hpp, i_schema_store.hpp)
│   │   │   │   ├── checkpoint/      (i_checkpoint_manager.hpp, i_recovery_manager.hpp)
│   │   │   │   ├── compression/     (i_compression_codec.hpp)
│   │   │   │   ├── io/              (i_file_system.hpp, i_clock.hpp, i_logger.hpp)
│   │   │   │   ├── scheduler/       (i_task.hpp, i_task_scheduler.hpp)
│   │   │   │   ├── storage/         (i_block_manager.hpp, i_buffer_manager.hpp,
│   │   │   │   │                     i_buffer_handle.hpp, i_wal.hpp)
│   │   │   │   └── transaction/     (i_transaction.hpp, i_transaction_manager.hpp,
│   │   │   │                         i_version_manager.hpp)
│   │   │   ├── catalog/
│   │   │   ├── checkpoint/          (checkpoint_manager.hpp, recovery_manager.hpp)
│   │   │   ├── execution/           (executor.hpp, pipeline.hpp, operators/, join/, aggregate/)
│   │   │   ├── profiler/
│   │   │   ├── scheduler/
│   │   │   ├── storage/
│   │   │   │   ├── buffer_manager.hpp
│   │   │   │   ├── block_file.hpp
│   │   │   │   ├── wal.hpp
│   │   │   │   ├── partition_info.hpp
│   │   │   │   ├── columnar/        (row_group.hpp, column_chunk.hpp,
│   │   │   │   │                     column_segment.hpp, version_info.hpp)
│   │   │   │   └── compression/     (bit_packing.hpp, delta.hpp, dictionary.hpp, rle.hpp)
│   │   │   └── transaction/         (mvcc.hpp, undo.hpp, version_manager.hpp)
│   │   └── src/ (mirrors include/ tree, minus abstractions/ — interfaces have no .cpp)
│   ├── infrastructure/
│   │   ├── CMakeLists.txt
│   │   ├── include/cppcoldb/infrastructure/
│   │   │   ├── io/       (posix_file_system.hpp)
│   │   │   ├── time/     (system_clock.hpp)
│   │   │   └── logging/  (console_logger.hpp, null_logger.hpp)
│   │   └── src/ (mirrors include/ tree)
│   ├── sql/
│   │   ├── CMakeLists.txt
│   │   ├── include/cppcoldb/sql/
│   │   │   ├── parser/   (tokenizer.hpp, parser.hpp, ast.hpp)
│   │   │   └── planner/  (binder.hpp, optimizer.hpp, logical_planner.hpp, physical_planner.hpp)
│   │   └── src/ (mirrors include/ tree)
│   ├── main/
│   │   ├── CMakeLists.txt
│   │   ├── include/cppcoldb/ (database.hpp, connection.hpp, client_context.hpp)
│   │   └── src/ (mirrors include/ tree)
│   └── cli/
│       ├── CMakeLists.txt
│       └── src/main.cpp
└── test/
    ├── CMakeLists.txt
    ├── common/          (mirrors src/common/, one test file per header)
    ├── engine/          (mirrors src/engine/, + fakes/ implementing engine abstractions)
    ├── infrastructure/  (mirrors src/infrastructure/)
    ├── sql/             (mirrors src/sql/)
    ├── main/            (mirrors src/main/)
    ├── integration/
    └── benchmark/
```

## 2. Dependency graph

One-directional; no cycles. `common` is the only leaf.

```text
                         common  (leaf: types/, primitives/, exception, not_implemented)
                            ^
              +-------------+-------------+
              |             |             |
           engine      infrastructure    sql
        (common)      (common, engine)  (common, engine)
              ^             ^             ^
              +-------------+-------------+
                            |
                          main
              (common, engine, infrastructure, sql)
                            ^
                            |
                          cli
                         (main)
```

Reading the arrows as "depends on, points up to": `engine` depends only on `common`.
`infrastructure` and `sql` each depend on `common` and `engine`, but never on each other.
`main` is the composition root and is the only module allowed to depend on all four.
`cli` depends only on `main` — it must never reach into `engine`, `sql`, or
`infrastructure` directly.

## 3. Module → target → dependencies → namespaces

| Module           | CMake target              | Depends on (PUBLIC unless noted)                              | Key namespaces |
|------------------|----------------------------|-----------------------------------------------------------------|----------------|
| `common`         | `cppcoldbv2_common`         | (none — leaf)                                                    | `cppcoldb::common` |
| `engine`         | `cppcoldbv2_engine`          | `cppcoldbv2_common`                                              | `cppcoldb::engine::storage`, `::catalog`, `::transaction`, `::checkpoint`, `::execution`, `::scheduler`, `::profiler`, `::io` (abstractions/io only) |
| `infrastructure` | `cppcoldbv2_infrastructure`  | `cppcoldbv2_common`, `cppcoldbv2_engine`                         | `cppcoldb::infrastructure::io`, `::time`, `::logging` |
| `sql`            | `cppcoldbv2_sql`             | `cppcoldbv2_common`, `cppcoldbv2_engine`                         | `cppcoldb::sql::parser`, `cppcoldb::sql::planner` |
| `main`           | `cppcoldbv2`                 | `cppcoldbv2_common`, `cppcoldbv2_engine`, `cppcoldbv2_infrastructure`, `cppcoldbv2_sql` | `cppcoldb` (root) |
| `cli`            | `cppcoldbv2_cli` (exe)       | `cppcoldbv2` (PRIVATE)                                           | `cppcoldb` (root, `main()` entry) |
| test targets     | one per module + integration + benchmark | PRIVATE the module under test (+ `cppcoldbv2_engine` for fakes) | mirrors the module under test |

## 4. Interface inventory (`I*` abstractions)

All interfaces live under `engine/include/cppcoldb/engine/abstractions/<domain>/`, use the
same namespace as their concrete implementation, declare a `virtual ~I* () = default;`,
and every method is pure (`= 0`). They include only `common` types and other
abstractions — never a concrete header.

| Domain        | Interfaces                                                    | Namespace |
|---------------|----------------------------------------------------------------|-----------|
| storage       | `IBlockManager`, `IBufferManager`, `IBufferHandle`, `IWal`      | `cppcoldb::engine::storage` |
| catalog       | `ICatalog`, `ISchemaStore`                                      | `cppcoldb::engine::catalog` |
| transaction   | `ITransaction`, `ITransactionManager`, `IVersionManager`        | `cppcoldb::engine::transaction` |
| checkpoint    | `ICheckpointManager`, `IRecoveryManager`                        | `cppcoldb::engine::checkpoint` |
| compression   | `ICompressionCodec`                                             | `cppcoldb::engine::storage` (codecs live alongside columnar storage) |
| scheduler     | `ITask`, `ITaskScheduler`                                       | `cppcoldb::engine::scheduler` |
| io (ports)    | `IFileSystem`, `IClock`, `ILogger`                              | `cppcoldb::engine::io` |

Adapters implementing the `io` ports live in `infrastructure/`:

| Port         | Adapter            | Namespace |
|--------------|---------------------|-----------|
| `IFileSystem`| `PosixFileSystem`   | `cppcoldb::infrastructure::io` |
| `IClock`     | `SystemClock`       | `cppcoldb::infrastructure::time` |
| `ILogger`    | `ConsoleLogger`, `NullLogger` | `cppcoldb::infrastructure::logging` |

## 5. Build sequence reference

See `design/v2-architecture.html` for the interactive version of this list; the same ten
steps are summarized here for a text-only reading:

1. `common` types/primitives
2. `engine/storage` block file, buffer manager, WAL
3. `engine/storage` columnar model + compression codecs
4. `engine/catalog` + `engine/transaction` (MVCC)
5. `engine/checkpoint` (checkpoint + recovery)
6. `engine/execution` runtime + physical operators
7. `sql/parser` + `sql/planner`
8. `main` facade wiring (composition root)
9. `engine/profiler` + `engine/scheduler`
10. `cli`
