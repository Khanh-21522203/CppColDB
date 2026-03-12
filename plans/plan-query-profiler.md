# Feature: Query Profiler

## 1. Purpose

The Query Profiler records per-phase timing and per-operator statistics for a single query execution. It is activated either by the `EXPLAIN ANALYZE` statement or by a per-connection profiling flag. The profiler adds no overhead when disabled. When enabled, it wraps each query phase (parse, bind, optimize, physical plan, execute) with wall-clock timing calls and wraps each physical operator call with a lightweight start/stop timer. Results are returned as a `ProfilingResult` structure, formatted as a human-readable plan tree when `EXPLAIN ANALYZE` is used.

## 2. Responsibilities

- `QueryProfiler::StartQuery(sql)`: record the query string and wall-clock start time
- `QueryProfiler::StartPhase(phase)` / `EndPhase(phase)`: record duration of each named phase (PARSE, BIND, OPTIMIZE, PHYSICAL_PLAN, EXECUTE)
- `QueryProfiler::EndQuery()`: compute total elapsed time; assemble the `ProfilingResult`
- `OperatorProfiler`: per-operator timing attached to each `PhysicalOperator`; records call count, total time, rows in, rows out
- `ProfilingResult::ToString()`: format the result as a text plan tree with timing and row counts (used for `EXPLAIN ANALYZE` output)
- `ClientContext` integration: wrap each query phase with profiler calls when `profiling_enabled_` is true
- `ExplainStatement` execution: run the inner statement with profiling enabled; return `ProfilingResult` as the query result

## 3. Non-Responsibilities

- Does not provide per-column or per-block I/O statistics
- Does not persist profiling data across queries
- Does not support JSON output (text only for the educational scope)
- Does not add timing overhead when profiling is disabled (profiler checks `profiling_enabled_` before recording)
- Does not profile background tasks

## 4. Architecture Design

```
ClientContext::Query(sql)  [profiling_enabled_ = true]
    |
    +-- QueryProfiler::StartQuery(sql)
    |
    +-- Phase: PARSE
    |     StartPhase(PARSE) → Parser::Parse() → EndPhase(PARSE)
    |
    +-- Phase: BIND
    |     StartPhase(BIND)  → Binder::Bind() → EndPhase(BIND)
    |
    +-- Phase: OPTIMIZE
    |     StartPhase(OPTIMIZE) → Optimizer::Optimize() → EndPhase(OPTIMIZE)
    |
    +-- Phase: PHYSICAL_PLAN
    |     StartPhase(PHYSICAL_PLAN) → PhysicalPlanner::Plan() → EndPhase(PHYSICAL_PLAN)
    |
    +-- Phase: EXECUTE
    |     StartPhase(EXECUTE)
    |     Executor::Execute()
    |       For each operator call:
    |         OperatorProfiler::StartOperator(op) → op.Execute() → EndOperator()
    |           records: time_ns, rows_in, rows_out
    |     EndPhase(EXECUTE)
    |
    +-- QueryProfiler::EndQuery()
    |
    +-- Return ProfilingResult (via EXPLAIN ANALYZE) or attach to QueryResult
```

## 5. Core Data Structures (C++)

```cpp
// src/main/query_profiler.hpp
#pragma once
#include <string>
#include <vector>
#include <chrono>
#include <unordered_map>
#include <cstdint>

namespace cppcoldb {

enum class QueryPhase {
    PARSE,
    BIND,
    OPTIMIZE,
    PHYSICAL_PLAN,
    EXECUTE,
};

// Timing data for one query phase.
struct PhaseProfile {
    QueryPhase  phase;
    std::string phase_name;
    int64_t     duration_us = 0; // microseconds
};

// Per-operator statistics.
struct OperatorProfile {
    std::string operator_name;   // e.g. "PhysicalTableScan", "PhysicalFilter"
    int64_t     total_time_ns;   // total nanoseconds spent in this operator
    int64_t     call_count;      // number of Execute/GetData calls
    int64_t     rows_in;         // total input rows processed
    int64_t     rows_out;        // total output rows produced
};

// Full profiling result for one query.
struct ProfilingResult {
    std::string                 sql;              // original SQL string
    int64_t                     total_duration_us; // wall clock, microseconds
    std::vector<PhaseProfile>   phases;           // in execution order
    std::vector<OperatorProfile> operators;       // in pipeline execution order

    // Format as a human-readable EXPLAIN ANALYZE plan tree.
    std::string ToString() const;
};

class QueryProfiler {
public:
    QueryProfiler() = default;

    // Call at the start of a query.
    void StartQuery(const std::string& sql);

    // Call at the start and end of each named phase.
    void StartPhase(QueryPhase phase);
    void EndPhase(QueryPhase phase);

    // Call at the end of a query; assembles ProfilingResult.
    ProfilingResult EndQuery();

    // Register an operator profile slot; returns its index in operators_.
    size_t RegisterOperator(const std::string& name);

    // Called by OperatorProfiler on each operator invocation.
    void RecordOperatorCall(size_t idx, int64_t duration_ns,
                            int64_t rows_in, int64_t rows_out);

    bool IsActive() const { return active_; }

private:
    bool   active_ = false;

    std::string sql_;
    std::chrono::steady_clock::time_point query_start_;

    struct PhaseTimer {
        QueryPhase phase;
        std::chrono::steady_clock::time_point start;
    };
    PhaseTimer current_phase_;

    std::vector<PhaseProfile>   phase_profiles_;
    std::vector<OperatorProfile> operator_profiles_;
};

} // namespace cppcoldb
```

```cpp
// src/execution/operator_profiler.hpp
#pragma once
#include <chrono>
#include <cstdint>
#include "main/query_profiler.hpp"

namespace cppcoldb {

// RAII guard that times one operator call.
class OperatorProfileGuard {
public:
    OperatorProfileGuard(QueryProfiler& profiler, size_t op_idx,
                         int64_t rows_in)
        : profiler_(profiler), op_idx_(op_idx), rows_in_(rows_in),
          start_(std::chrono::steady_clock::now()) {}

    ~OperatorProfileGuard() {
        auto end = std::chrono::steady_clock::now();
        int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end - start_).count();
        profiler_.RecordOperatorCall(op_idx_, ns, rows_in_, rows_out_);
    }

    void SetRowsOut(int64_t n) { rows_out_ = n; }

private:
    QueryProfiler&                         profiler_;
    size_t                                 op_idx_;
    int64_t                                rows_in_;
    int64_t                                rows_out_ = 0;
    std::chrono::steady_clock::time_point  start_;
};

} // namespace cppcoldb
```

## 6. Public Interfaces

```cpp
namespace cppcoldb {

class QueryProfiler {
public:
    void            StartQuery(const std::string& sql);
    void            StartPhase(QueryPhase phase);
    void            EndPhase(QueryPhase phase);
    ProfilingResult EndQuery();
    size_t          RegisterOperator(const std::string& name);
    void            RecordOperatorCall(size_t idx, int64_t duration_ns,
                                       int64_t rows_in, int64_t rows_out);
    bool            IsActive() const;
};

// RAII guard for operator timing:
class OperatorProfileGuard {
public:
    OperatorProfileGuard(QueryProfiler&, size_t op_idx, int64_t rows_in);
    ~OperatorProfileGuard(); // records timing and row counts
    void SetRowsOut(int64_t n);
};

struct ProfilingResult {
    std::string                  sql;
    int64_t                      total_duration_us;
    std::vector<PhaseProfile>    phases;
    std::vector<OperatorProfile> operators;
    std::string                  ToString() const;
};

} // namespace cppcoldb
```

## 7. Internal Algorithms

### QueryProfiler::StartPhase / EndPhase
```
StartPhase(phase):
  if not active_: return
  current_phase_ = {phase, steady_clock::now()}

EndPhase(phase):
  if not active_: return
  end = steady_clock::now()
  duration_us = duration_cast<microseconds>(end - current_phase_.start).count()
  phase_profiles_.push_back({phase, PhaseName(phase), duration_us})
  Time: O(1)
```

### QueryProfiler::EndQuery
```
EndQuery():
  end = steady_clock::now()
  total_us = duration_cast<microseconds>(end - query_start_).count()
  result = ProfilingResult{sql_, total_us, phase_profiles_, operator_profiles_}
  active_ = false
  phase_profiles_.clear()
  operator_profiles_.clear()
  return result
  Time: O(phases + operators)
```

### ProfilingResult::ToString
```
ToString():
  output = ""
  output += "Query: " + sql + "\n"
  output += "Total: " + to_string(total_duration_us / 1000.0) + " ms\n\n"
  output += "Phases:\n"
  for phase in phases:
    output += "  " + phase.phase_name + ": " +
              to_string(phase.duration_us / 1000.0) + " ms\n"
  output += "\nOperators:\n"
  for op in operators:
    pct = op.total_time_ns / total_ns * 100.0
    output += "  " + op.operator_name +
              "  time=" + to_string(op.total_time_ns / 1e6) + "ms" +
              "  rows_in=" + to_string(op.rows_in) +
              "  rows_out=" + to_string(op.rows_out) +
              "  calls=" + to_string(op.call_count) +
              "  (" + to_string(pct) + "%)\n"
  return output
  Time: O(phases + operators)
```

### ClientContext integration (when profiling enabled)
```
Query(sql):
  profiler_.StartQuery(sql)

  profiler_.StartPhase(PARSE)
  stmt = parser.Parse(sql)
  profiler_.EndPhase(PARSE)

  profiler_.StartPhase(BIND)
  plan = binder.Bind(stmt)
  profiler_.EndPhase(BIND)

  profiler_.StartPhase(OPTIMIZE)
  opt = optimizer.Optimize(plan)
  profiler_.EndPhase(OPTIMIZE)

  profiler_.StartPhase(PHYSICAL_PLAN)
  phy = physical_planner.Plan(opt)
  profiler_.EndPhase(PHYSICAL_PLAN)

  // Register each operator with profiler before execution
  RegisterOperatorProfiles(phy, profiler_)

  profiler_.StartPhase(EXECUTE)
  executor.Execute()
  profiler_.EndPhase(EXECUTE)

  result = profiler_.EndQuery()
  // If EXPLAIN ANALYZE: return result.ToString() as a text QueryResult
  // Otherwise: attach result to QueryResult for inspection
```

### PipelineExecutor: per-operator profiling hook
```
// Inside PipelineExecutor::Execute(), for each operator call:
if ctx.profiler.IsActive():
  guard = OperatorProfileGuard(ctx.profiler, op.profile_idx, input.count)
  result = op.Execute(input, output, state, ctx)
  guard.SetRowsOut(output.count)
  // guard destructor fires here → records timing + rows
else:
  result = op.Execute(input, output, state, ctx)
```

## 8. Persistence Model

Not applicable. Profiling data is ephemeral; it exists only for the duration of a query.

## 9. Concurrency Model

`QueryProfiler` is owned by `ClientContext` and is never shared across threads. No locks needed.

## 10. Configuration

```cpp
// Per-connection flag (set on Connection object):
struct ConnectionConfig {
    bool profiling_enabled = false; // set true to profile all queries on this connection
};

// EXPLAIN ANALYZE: forces profiling_enabled = true for that single query.
```

## 11. Testing Strategy

- `TestProfilerPhaseTimings`: run a query with profiling enabled → all 5 phases present in ProfilingResult
- `TestProfilerParsePhaseDuration`: parse a complex query → parse duration > 0 µs
- `TestProfilerTotalTime`: total_duration_us >= sum of all phase durations
- `TestProfilerOperatorRowCounts`: run a filter that passes 50% of rows → rows_in == total, rows_out == 50%
- `TestProfilerDisabledIsNoop`: profiling disabled → no timing overhead (profiler calls return immediately)
- `TestExplainAnalyzeReturnsText`: `EXPLAIN ANALYZE SELECT * FROM t` → QueryResult contains formatted text
- `TestExplainAnalyzeOutputContainsPhases`: output includes "PARSE", "BIND", "OPTIMIZE", "EXECUTE"
- `TestExplainAnalyzeOutputContainsOperators`: output includes "PhysicalTableScan", "PhysicalFilter"
- `TestOperatorProfileGuardRAII`: guard destroyed → RecordOperatorCall called exactly once
- `TestProfilerMultipleOperators`: pipeline with 3 operators → 3 OperatorProfile entries
- `TestProfilerResetBetweenQueries`: run two queries in sequence → profiles do not accumulate across queries

## 12. Open Questions

- Per-column I/O statistics: bytes read per column, block cache hit/miss counts — useful for storage tuning but deferred.
- JSON output format: could be added as a second ToString() variant for tooling consumption.
