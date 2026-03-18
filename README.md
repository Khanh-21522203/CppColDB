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
  - Table scan with zone-map segment pruning and late materialization
  - Filter, projection, limit
  - Hash aggregation
  - Hash join with vectorized batch probe
  - Sort (`ORDER BY`) with sink/source two-pipeline pattern and top-k heap
  - Mutation operators for `INSERT/UPDATE/DELETE` and DDL
- Storage and durability:
  - Columnar storage (`RowGroup` + `ColumnChunk` + compressed segments)
  - Compression codecs: uncompressed, RLE, bit-packed, delta, dictionary
  - MVCC visibility markers
  - WAL write/replay (`INSERT/DELETE/UPDATE/DDL`)
  - Checkpointing and WAL truncation
- Query profiling infrastructure and phase/operator profiling tests
- Performance optimizations:
  - memcpy-based bulk column copy (eliminates per-row variant dispatch)
  - Batched rollback (`WriteRows` per undo entry vs per-row `WriteRow`)
  - Zone-map segment skipping (min/max stats per segment)
  - Late materialization (filter columns scanned first; payload deferred to passing rows)
  - Vectorized hash join probe (batch hash over typed arrays, no `Value` boxing)
  - O(1) MVCC fast path (counter-based `AllInsertedVisibleTo` skips per-row map lookups on clean scans)

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
| `read.count_scan` | 1.780 | 1.760 | 1.887 | 1.731 | 1.917 | 561.754 |
| `read.filter_count` | 1.212 | 1.213 | 1.221 | 1.201 | 1.229 | 825.123 |
| `read.join_orderby_limit` | 19.576 | 19.420 | 20.284 | 19.216 | 20.412 | 51.084 |
| `read.orderby_limit` | 3.087 | 3.037 | 3.462 | 2.868 | 3.661 | 323.969 |
| `read.filter_project` | 1.772 | 1.710 | 1.978 | 1.645 | 2.066 | 564.435 |
| `write.insert_rollback` | 0.224 | 0.223 | 0.236 | 0.214 | 0.250 | 4467.196 |
| `write.update_rollback` | 5.989 | 5.910 | 6.621 | 5.564 | 6.730 | 166.983 |
| `write.delete_rollback` | 2.694 | 2.602 | 3.224 | 2.487 | 3.307 | 371.247 |

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