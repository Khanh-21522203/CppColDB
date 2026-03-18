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
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release
cmake --build build_release -j --target benchmark_cppcoldb
./build_release/benchmark_cppcoldb --rows 100000 --warmup 2 --iters 8
```

| Case | avg_ms | p50 | p95 | min | max | qps |
|---|---:|---:|---:|---:|---:|---:|
| `read.count_scan` | 2.075 | 2.058 | 2.202 | 1.999 | 2.270 | 481.987 |
| `read.filter_count` | 1.953 | 1.682 | 2.791 | 1.583 | 2.906 | 512.003 |
| `read.join_orderby_limit` | 22.081 | 21.885 | 23.630 | 20.993 | 24.380 | 45.289 |
| `read.orderby_limit` | 3.370 | 3.366 | 3.533 | 3.125 | 3.561 | 296.729 |
| `write.insert_rollback` | 0.215 | 0.212 | 0.225 | 0.210 | 0.225 | 4643.843 |
| `write.update_rollback` | 5.698 | 5.701 | 5.970 | 5.346 | 6.032 | 175.500 |
| `write.delete_rollback` | 2.748 | 2.781 | 2.939 | 2.546 | 2.987 | 363.884 |

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
