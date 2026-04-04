# Query Profiling and EXPLAIN ANALYZE

## Purpose

Collect phase/operator timing and row-count telemetry per query, and expose it through `EXPLAIN ANALYZE` output and `QueryResult.profiling_result`.

## Scope

**In scope:**
- Query-phase timing (`PARSE`, `BIND`, `OPTIMIZE`, `PHYSICAL_PLAN`, `EXECUTE`).
- Operator-level profiling registration and call metrics.
- `EXPLAIN ANALYZE` interception behavior in `ClientContext::Query`.

**Out of scope:**
- Query execution itself.
- Persistent telemetry storage/export.

## Primary User Flow

1. Caller executes normal query with profiling enabled or executes `EXPLAIN ANALYZE ...`.
2. `ClientContext::Query` starts/ends phase timers around pipeline stages.
3. `Executor` registers operators and runtime calls use `OperatorProfileGuard`.
4. `QueryProfiler::EndQuery` returns `ProfilingResult`.
5. `EXPLAIN ANALYZE` returns profiling text as single `QUERY PLAN` VARCHAR row.

## System Flow

1. `ClientContext::Query` handles plain `EXPLAIN` without execution by formatting logical plan.
2. For `EXPLAIN ANALYZE`, it forces profiling on if needed and executes inner statement.
3. `Executor::Execute` calls `RegisterOperators` (demangled class names) before running pipelines.
4. `OperatorProfileGuard` records per-call duration, rows in, rows out in RAII destructor.
5. `ProfilingResult::ToString` emits human-readable phase and operator sections.

```
ClientContext::Query
  -> StartQuery / StartPhase...
  -> execute pipeline
  -> EndQuery -> ProfilingResult
  -> normal query: attach profiling_result
  -> EXPLAIN ANALYZE: return QUERY PLAN text
```

## Data Model

- `QueryPhase` enum (`src/profiler/profiling_result.hpp`):
- `PARSE`, `BIND`, `OPTIMIZE`, `PHYSICAL_PLAN`, `EXECUTE`
- `PhaseProfile`:
- `phase`, `phase_name`, `duration_us`
- `OperatorProfile`:
- `operator_name`, `total_time_ns`, `call_count`, `rows_in`, `rows_out`
- `ProfilingResult`:
- `sql`, `total_duration_us`, `phases`, `operators`
- `QueryProfiler` internal state:
- `active_`, `query_start_`, `phase_profiles_`, `operator_profiles_`

## Interfaces and Contracts

- `void QueryProfiler::StartQuery(const std::string&)`
- contract: resets phase/operator accumulators for new query.
- `StartPhase/EndPhase(QueryPhase)`
- contract: no-op when profiler is inactive.
- `size_t RegisterOperator(const std::string& name)`
- contract: returns index used for later `RecordOperatorCall`.
- `ProfilingResult EndQuery()`
- contract: finalizes total duration and deactivates profiler.
- `OperatorProfileGuard` (`src/profiler/operator_profiler.hpp`)
- contract: records one operator call on destruction.

## Dependencies

**Internal modules:**
- `src/main/client_context.cpp` for phase boundaries and explain behavior.
- `src/execution/executor.cpp` for operator registration.
- `src/execution/pipeline_executor.cpp` for guarded operator/source/sink call timing.

**External services/libraries:**
- C++ chrono library only.

## Failure Modes and Edge Cases

- Calling phase methods while inactive is intentionally ignored.
- `RegisterOperator` appends even if profiler is inactive; useful but easy to misunderstand in standalone usage.
- Plain `EXPLAIN` returns logical-plan text and does not execute operators.
- On exceptions in query pipeline, profiler is ended/discarded in catch blocks.

## Observability and Debugging

- Profiling tests: `test/profiler/test_query_profiler.cpp`.
- Runtime string format source: `src/profiler/query_profiler.cpp:ProfilingResult::ToString`.
- Query debug path: `src/main/client_context.cpp` around explain/profiling branches.

## Risks and Notes

- Profiling output is textual and in-memory only; no persistent historical store.
- Operator naming depends on C++ type demangling and may vary by toolchain.

Changes:

