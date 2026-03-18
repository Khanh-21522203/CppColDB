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
| `read.count_scan` | 3.095 | 3.076 | 3.187 | 3.054 | 3.225 | 323.095 |
| `read.filter_count` | 1.902 | 1.865 | 2.035 | 1.850 | 2.079 | 525.817 |
| `read.join_orderby_limit` | 23.395 | 23.394 | 23.790 | 23.005 | 23.903 | 42.745 |
| `read.orderby_limit` | 4.663 | 4.645 | 4.995 | 4.382 | 5.101 | 214.456 |
| `write.insert_rollback` | 0.213 | 0.213 | 0.217 | 0.211 | 0.218 | 4687.870 |
| `write.update_rollback` | 151.701 | 151.609 | 153.161 | 149.819 | 153.267 | 6.592 |
| `write.delete_rollback` | 4.314 | 4.175 | 5.245 | 3.808 | 5.679 | 231.784 |

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
