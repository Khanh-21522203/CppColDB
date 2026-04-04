# CppColDB — Design Index

CppColDB is a C++ embedded SQL column-store database with a CLI REPL runtime (`main.cpp`) and optional persistent storage mode (`src/main/database.cpp`), combining parse/bind/optimize/physical-plan/execution pipelines with MVCC transactions, WAL/checkpoint recovery, partitioning, and a built-in benchmark/profiling toolchain.

## Feature Matrix

| Feature | Description | File | Status |
|---------|-------------|------|--------|
| SQL Entry Points and Session Lifecycle | Database bootstrapping, connection lifecycle, and SQL dispatch from API/CLI into query processing. | [sql-entrypoints.md](sql-entrypoints.md) | Stable |
| SQL Parser and AST | Tokenization and parser grammar producing statement/expression AST nodes, including partition DDL syntax. | [sql-parser-ast.md](sql-parser-ast.md) | Stable |
| Query Planning Pipeline | Binder, logical-plan construction, logical rewrites, and physical operator selection. | [query-planning.md](query-planning.md) | Stable |
| Execution Pipelines | Pipeline graph construction, operator pull execution, and per-operator profiling integration. | [execution-pipelines.md](execution-pipelines.md) | Stable |
| Catalog, DDL, and Metadata Visibility | Schema/table/index/partition metadata management with MVCC-aware visibility and serialization. | [catalog-ddl-metadata.md](catalog-ddl-metadata.md) | Stable |
| Transactions, Undo, and MVCC | Transaction lifecycle, undo buffers, commit/rollback application, and row visibility rules. | [transaction-mvcc.md](transaction-mvcc.md) | Stable |
| Buffer Pool and Block File Storage | Block-file persistence and cached pin/unpin LRU buffer management for page I/O. | [buffer-and-block-storage.md](buffer-and-block-storage.md) | Stable |
| Columnar Storage and Compression | Row-group/column-chunk segment storage with codec selection and version-aware scans. | [columnar-storage-compression.md](columnar-storage-compression.md) | Stable |
| WAL, Checkpoint, and Recovery | WAL append/replay durability flow and checkpoint materialization/restart logic. | [wal-checkpoint-recovery.md](wal-checkpoint-recovery.md) | Stable |
| Partition Routing and Pruning | HASH/RANGE/LIST partition rule validation, insert routing, and scan-time pruning. | [partitioning-routing-pruning.md](partitioning-routing-pruning.md) | Stable |
| Query Profiling and Explain Analyze | Runtime profiling collection and text rendering for `EXPLAIN ANALYZE` style diagnostics. | [query-profiling.md](query-profiling.md) | Stable |
| Task Scheduler and Async Checkpoint Dispatch | Thread-pool task queue and background checkpoint scheduling API surface. | [task-scheduler.md](task-scheduler.md) | In Progress |
| Benchmark Harness | Synthetic workload generation and throughput/latency measurement executable. | [benchmark-harness.md](benchmark-harness.md) | Stable |

## Cross-Cutting Concerns

Transaction state (`src/transaction/transaction_manager.cpp`) and row visibility (`src/storage/column/version_info.cpp`) cut across catalog mutation, scan/filter execution, and partitioned table writes. Durability flows coordinate `src/transaction/wal.cpp`, checkpoint materialization in `src/checkpoint/checkpoint_manager.cpp`, and startup replay in `src/main/database.cpp`. Error handling is mostly exception-based (`DBException` paths in parser/planner/storage) with selective catch-and-convert at connection boundaries (`src/main/connection.cpp`). Observability is lightweight and mostly local: query/operator timing is surfaced by `src/profiler/query_profiler.cpp`, while scheduler and shutdown paths rely on stderr/log messages with minimal structured metrics.

## Notes

