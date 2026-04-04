# Benchmark Harness

## Purpose

Provide a reproducible CLI benchmark executable and helper script for measuring CppColDB query/update workloads.

## Scope

**In scope:**
- `benchmark_cppcoldb` executable (`test/benchmark/benchmark_main.cpp`).
- CLI argument parsing and workload setup.
- Case execution timing/statistics computation.
- Shell helper script `scripts/run_benchmark.sh`.

**Out of scope:**
- Database engine internals being measured.
- Long-term metrics storage or visualization.

## Primary User Flow

1. Build benchmark target (`benchmark_cppcoldb`).
2. Run executable with flags (`--rows`, `--warmup`, `--iters`, `--insert-batch`, `--db`).
3. Harness creates schema/data, runs warmup then measured rounds per case.
4. Harness prints per-case `avg/p50/p95/min/max/qps/rows_out` table.

## System Flow

1. `ParseArgs` validates CLI inputs and populates `BenchConfig`.
2. `Database db(cfg.db_path)` and `conn = db.Connect()` initialize runtime.
3. `PopulateBaseData` creates base tables and inserts generated rows.
4. Benchmark iterates `BenchCase` statements via `ExecCase` and records wall-clock milliseconds.
5. `RunBenchmarkCase` computes percentiles and derived metrics (`qps`, average output rows).
6. `run_benchmark.sh` optionally configures/builds target and forwards env var overrides to CLI flags.

## Data Model

- `BenchConfig` fields:
- `rows`, `warmup`, `iterations`, `insert_batch`, `db_path`
- `BenchCase`:
- `name (string)`, `statements (vector<string>)`
- `BenchStats`:
- `avg_ms`, `p50_ms`, `p95_ms`, `min_ms`, `max_ms`, `qps`, `avg_rows_out`

## Interfaces and Contracts

- Executable usage:
- `benchmark_cppcoldb [--rows N] [--warmup N] [--iters N] [--insert-batch N] [--db PATH]`
- contract: invalid/unknown args call `Fail(...)` and exit non-zero.
- SQL execution helper `RunSQL` aborts process on query failure.
- Script `scripts/run_benchmark.sh`:
- builds target with CMake and runs benchmark with env defaults (`ROWS`, `WARMUP`, `ITERS`, `INSERT_BATCH`, `DB_PATH`).

## Dependencies

**Internal modules:**
- `src/main/database.hpp`, `src/main/connection.hpp` via public embedding API.

**External services/libraries:**
- C++ chrono/statistics utilities.
- CMake and shell tooling for script path.

## Failure Modes and Edge Cases

- Any failed SQL in setup or benchmark case aborts execution (`Fail`).
- `ParseArgs` rejects non-positive counts for rows/iterations/batch and negative warmup.
- Benchmark measures end-to-end SQL execution time including parsing/planning/execution and transaction control statements in multi-statement cases.

## Observability and Debugging

- Output is a fixed-width terminal table printed by benchmark executable.
- Quick debug points:
- workload composition: `cases` vector in `benchmark_main.cpp`
- data generation: `BuildInsertSQL`, `PopulateBaseData`

## Risks and Notes

- Timing is wall-clock based and single-process; results include host variability and are not isolated microbenchmarks.
- Workload cases include rollback write patterns, which stress transaction paths differently from committed write-heavy workloads.

Changes:

