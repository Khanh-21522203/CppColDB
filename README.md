# CppColDB

CppColDB is a C++20 learning project that builds a small column-oriented relational database from scratch.

It is an embedded database engine, not a separate server process. Applications create a `Database` object and use `Connection` objects in-process.

## What Is Implemented

- SQL parser and AST for:
  - `SELECT`, `INSERT`, `UPDATE`, `DELETE`
  - `CREATE TABLE`, `DROP TABLE`
  - `BEGIN`, `COMMIT`, `ROLLBACK`
  - `EXPLAIN`, `EXPLAIN ANALYZE`
- Query pipeline:
  - Binder (semantic analysis)
  - Logical optimizer (predicate pushdown, column pruning, constant folding)
  - Physical planner
  - Vectorized executor with pipeline dependencies
- Physical operators:
  - Table scan, filter, projection, limit
  - Hash aggregation
  - Hash join
  - Sort (`ORDER BY`) with sink/source two-pipeline pattern
  - Mutation operators for `INSERT/UPDATE/DELETE` and DDL
- Storage and durability:
  - Columnar storage (`RowGroup` + `ColumnChunk` + compressed segments)
  - MVCC visibility markers
  - WAL write/replay (`INSERT/DELETE/UPDATE/DDL`)
  - Checkpointing and WAL truncation
- Query profiling infrastructure and phase/operator profiling tests

## Architecture At A Glance

Request flow for a query:

`Connection::Query -> ClientContext::Query -> Parser -> Binder -> Optimizer -> PhysicalPlanner -> Executor/PipelineExecutor -> QueryResult`

Core storage flow:

`Catalog -> Table -> RowGroup -> ColumnChunk -> Segment blocks (via BufferManager/BlockFile)`

Durability flow:

`TransactionManager::Commit -> Transaction::WriteToWAL -> WAL::Flush -> commit-time apply -> CheckpointManager`

Recovery flow:

`Database startup -> Catalog deserialize from checkpoint block -> WAL replay after last checkpoint marker`

## Build

Requirements:

- CMake 3.20+
- C++20 compiler (GCC/Clang/MSVC)

Commands:

```bash
cmake -S . -B build
cmake --build build -j
```

## Run

In-memory mode:

```bash
./build/cppcoldb
```

Persistent mode:

```bash
./build/cppcoldb /tmp/mydb
```

REPL accepts one SQL statement per line. Exit with `exit`, `quit`, or `\\q`.

## Test

Run full test suite:

```bash
ctest --test-dir build --output-on-failure
```

CMake defines phase targets plus integration target:

- `test_phase0` ... `test_phase10`
- `test_integration`

## Benchmark

Build and run the built-in benchmark harness:

```bash
cmake -S . -B build
cmake --build build -j --target benchmark_cppcoldb
./build/benchmark_cppcoldb --rows 20000 --warmup 3 --iters 15
```

Useful flags:

- `--rows N` dataset size for benchmark setup
- `--warmup N` warmup rounds per case
- `--iters N` measured rounds per case
- `--insert-batch N` setup/insert batch size
- `--db PATH` use persistent DB path instead of `:memory:`

Run with helper script (prints results to terminal):

```bash
./scripts/run_benchmark.sh
```

Override workload size/rounds with environment variables:

```bash
ROWS=200000 WARMUP=4 ITERS=12 ./scripts/run_benchmark.sh
DB_PATH=/tmp/cppcoldb_bench ./scripts/run_benchmark.sh
```

Sample statistics:

```bash
./build/benchmark_cppcoldb --rows 100000 --warmup 2 --iters 8
```

| Case | avg_ms | p50 | p95 | min | max | qps |
|---|---:|---:|---:|---:|---:|---:|
| `read.count_scan` | 65.533 | 65.253 | 66.522 | 64.922 | 66.634 | 15.260 |
| `read.filter_count` | 42.961 | 42.611 | 43.975 | 42.473 | 44.046 | 23.277 |
| `read.join_orderby_limit` | 566.150 | 565.349 | 570.242 | 563.261 | 570.458 | 1.766 |
| `read.orderby_limit` | 411.457 | 411.276 | 414.335 | 408.388 | 414.449 | 2.430 |
| `write.insert_rollback` | 3.784 | 3.783 | 3.814 | 3.759 | 3.823 | 264.258 |
| `write.update_rollback` | 6358.477 | 6355.949 | 6402.653 | 6318.547 | 6423.370 | 0.157 |
| `write.delete_rollback` | 71.877 | 71.873 | 73.817 | 69.879 | 73.995 | 13.913 |

## Repository Layout

```text
.
├── CMakeLists.txt
├── main.cpp
├── src/
│   ├── common/
│   ├── parser/
│   ├── planner/
│   ├── execution/
│   ├── storage/
│   ├── catalog/
│   ├── transaction/
│   ├── checkpoint/
│   ├── parallel/
│   ├── profiler/
│   └── main/
├── test/
├── plans/
├── flows/
└── tasks/
```

## Notes

- This project is intended for learning and experimentation, not production use.
- Design docs and implementation plans are in `plans/`.
- Architecture diagrams are in `flows/`.
