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
