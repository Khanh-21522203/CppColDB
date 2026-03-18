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
  - O(1) MVCC fast path (`AllInsertedVisibleTo` skips per-row map lookups on clean scans)
  - Scratch `DataVector` in `ColumnScanState` (eliminates per-segment heap alloc in `Scan`)
  - Scalar aggregate fast path (no GROUP BY: one hash lookup per chunk, batch `count +=`)
  - Typed sort comparison (direct `int64`/`double`/`string` compare, no `Value` boxing)
  - Reusable `group_key` in `HashAggState` (pre-sized once, overwritten in-place per row)

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

REPL accepts one SQL statement per line. Exit with `exit`, `quit`, or `\q`.

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
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release
cmake --build build_release -j --target benchmark_cppcoldb
./build_release/benchmark_cppcoldb --rows 100000 --warmup 3 --iters 15
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

Sample statistics (100K rows, Release build):

| Case                        | avg_ms  | p50     | p95     | min     | max     | qps      |
|-----------------------------|--------:|--------:|--------:|--------:|--------:|---------:|
| `read.count_scan`           |   1.346 |   1.346 |   1.353 |   1.333 |   1.360 |  743.009 |
| `read.filter_count`         |   1.070 |   1.068 |   1.086 |   1.056 |   1.099 |  934.891 |
| `read.join_orderby_limit`   |  13.862 |  13.796 |  14.312 |  13.451 |  14.406 |   72.142 |
| `read.orderby_limit`        |   2.276 |   2.256 |   2.409 |   2.199 |   2.443 |  439.305 |
| `read.filter_project`       |   1.731 |   1.701 |   1.885 |   1.664 |   1.899 |  577.678 |
| `write.insert_rollback`     |   0.215 |   0.214 |   0.222 |   0.209 |   0.223 | 4651.441 |
| `write.update_rollback`     |   5.832 |   5.713 |   6.378 |   5.492 |   6.478 |  171.462 |
| `write.delete_rollback`     |   2.663 |   2.652 |   2.822 |   2.538 |   2.877 |  375.452 |

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
