# Query Profiling Flow

## Assumptions
- Profiling is opt-in per connection via a configuration flag or EXPLAIN ANALYZE.
- The QueryProfiler records wall-clock time for each major query phase.
- Operator-level timing is tracked per pipeline operator during execution.
- EXPLAIN ANALYZE triggers profiling for a single query and prints the result.

## Diagram

```mermaid
flowchart TD
    A["Client issues EXPLAIN ANALYZE query\nor profiling is enabled in config"] --> B["ClientContext::Query()\nsets profiling_enabled = true"]

    B --> C["QueryProfiler::StartQuery(sql)"]
    C --> D["Record query_start_time"]

    D --> E["--- PARSE PHASE ---"]
    E --> E1["QueryProfiler::StartPhase(PARSE)"]
    E1 --> E2["Parser::Parse(sql)"]
    E2 --> E3["QueryProfiler::EndPhase(PARSE)\nRecord parse_duration"]

    E3 --> F["--- BIND PHASE ---"]
    F --> F1["QueryProfiler::StartPhase(BIND)"]
    F1 --> F2["Binder::Bind(statement)"]
    F2 --> F3["QueryProfiler::EndPhase(BIND)\nRecord bind_duration"]

    F3 --> G["--- OPTIMIZE PHASE ---"]
    G --> G1["QueryProfiler::StartPhase(OPTIMIZE)"]
    G1 --> G2["Optimizer::Optimize(plan)"]
    G2 --> G3["QueryProfiler::EndPhase(OPTIMIZE)\nRecord optimize_duration"]

    G3 --> H["--- PHYSICAL PLAN PHASE ---"]
    H --> H1["QueryProfiler::StartPhase(PHYSICAL_PLAN)"]
    H1 --> H2["PhysicalPlanner::Plan(plan)"]
    H2 --> H3["QueryProfiler::EndPhase(PHYSICAL_PLAN)\nRecord physical_plan_duration"]

    H3 --> I["--- EXECUTE PHASE ---"]
    I --> I1["QueryProfiler::StartPhase(EXECUTE)"]
    I1 --> I2["Executor::Execute()"]

    I2 --> I3["For each operator in each pipeline"]
    I3 --> I4["OperatorProfiler::StartOperator(op)"]
    I4 --> I5["Operator::Execute() or Source::GetData()"]
    I5 --> I6["OperatorProfiler::EndOperator()\nRecord: time_ns, rows_in, rows_out"]
    I6 --> I3

    I2 --> I7["QueryProfiler::EndPhase(EXECUTE)\nRecord execute_duration"]

    I7 --> J["QueryProfiler::EndQuery()"]
    J --> K["Assemble ProfilingResult:\nPhase timings + per-operator stats"]

    K --> L{"Output\nformat?"}
    L -- "EXPLAIN ANALYZE text" --> M["ProfilingResult::ToString()\nPrint plan tree with timings and row counts"]
    L -- "Internal use" --> N["ProfilingResult stored in\nClientContext for inspection"]

    subgraph ProfilingData["ProfilingResult Data"]
        PD1["total_duration_ms"]
        PD2["phases: {PARSE, BIND, OPTIMIZE, PHYSICAL_PLAN, EXECUTE}"]
        PD3["operators: [{name, time_ns, rows_in, rows_out}, ...]"]
    end
```

## Planned Implementation
- `src/main/query_profiler.cpp` — QueryProfiler, StartPhase(), EndPhase(), StartQuery(), EndQuery()
- `src/execution/operator_profiler.cpp` — per-operator timing
- `src/main/client_context.cpp` — profiling integration in Query()
- `src/main/explain.cpp` — EXPLAIN ANALYZE output formatting
