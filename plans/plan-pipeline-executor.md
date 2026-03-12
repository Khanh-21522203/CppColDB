# Feature: Pipeline Executor

## 1. Purpose

The Pipeline Executor orchestrates the actual execution of a query. The `Executor` receives a `PhysicalPlan`, breaks it into one or more `Pipeline` objects (each a linear Source → Operators → Sink chain), and runs them in dependency order. The `PipelineExecutor` runs one `Pipeline` at a time in a chunk-at-a-time (vectorized) loop: it calls `Source::GetData()` repeatedly, passes each `DataChunk` through the operator chain, and feeds the result to the `Sink`. All query execution is single-threaded; the thread pool is only used for background tasks.

## 2. Responsibilities

- `Executor`: receive a `PhysicalOperator` tree, build `Pipeline` objects from it, run them in order
- `Pipeline`: represent a linear execution chain — exactly one `SOURCE` operator, zero or more `OPERATOR` nodes, exactly one `SINK` operator
- `PipelineExecutor`: drive the chunk loop for one pipeline; handle `FINISHED`, `HAVE_MORE_OUTPUT`, `NEEDS_MORE_INPUT` signals
- Initialize per-operator local state before execution begins (scan state, hash table handle, sort buffer)
- Finalize each operator and sink after the source is exhausted (flush buffered aggregation state, sort output, etc.)
- Abort remaining pipelines on error and propagate the exception
- Collect results from the `PhysicalResultCollector` sink and return them as a `QueryResult`

## 3. Non-Responsibilities

- Does not perform multi-threaded pipeline execution (single-threaded per query)
- Does not perform optimization or planning
- Does not interact with the WAL or transaction system directly (operators do that via `ClientContext`)
- Does not manage memory (that is `BufferManager`'s job)
- Does not serialize results to a wire protocol

## 4. Architecture Design

```
Executor::Initialize(physical_plan)
  |
  +-- Build Pipeline list from physical operator tree
  |     Pipeline 0: HashJoinBuild  (Source=right_scan, Sink=HashJoinBuildSink)
  |     Pipeline 1: ProbeAndProject (Source=left_scan, Op=HashJoinProbe, Op=Filter, Sink=ResultCollector)
  |
Executor::Execute()
  |
  for each Pipeline in dependency order:
    PipelineExecutor::Execute(pipeline)
      |
      +-- Source::InitScan(scan_state)
      |
      +-- loop:
      |     chunk.Reset()
      |     Source::GetData(scan_state, chunk)  → FINISHED or HAVE_MORE_OUTPUT
      |     if FINISHED: break
      |     for each Operator in chain:
      |       Operator::Execute(input_chunk, output_chunk)
      |     Sink::Consume(final_chunk)
      |
      +-- for each Operator: TryFlush()
      +-- Sink::Finalize()
```

**Pipeline dependency**: a `PhysicalHashJoinProbe` pipeline depends on the corresponding `PhysicalHashJoinBuild` pipeline completing first. The Executor sorts pipelines by dependency before running.

## 5. Core Data Structures (C++)

```cpp
// src/execution/operator_result_type.hpp
#pragma once

namespace cppcoldb {

// Returned by Source::GetData() and Operator::Execute() to signal what happened.
enum class OperatorResultType {
    // Source: there are no more rows to produce. The pipeline is done.
    // Operator: this input chunk produced the last output; stop the pipeline.
    FINISHED,

    // Source: this chunk is full (STANDARD_VECTOR_SIZE rows); call GetData() again.
    // Operator: this input chunk produced output AND the operator may produce more
    //           output from the SAME input chunk next call (e.g., a HashJoinProbe
    //           with a many-to-one match expanding one probe row into many output rows).
    //           → The executor must call Execute() again on the same input before
    //             fetching the next source chunk.
    HAVE_MORE_OUTPUT,

    // Operator only: this input chunk was consumed but produced no output yet
    // (e.g., HashJoinBuild absorbing rows; AggregateOperator accumulating state).
    // → The executor should fetch the next source chunk without calling Sink.
    NEEDS_MORE_INPUT,
};

} // namespace cppcoldb
```

```cpp
// src/execution/pipeline.hpp
#pragma once
#include <vector>
#include <memory>
#include "execution/physical_operator.hpp"
#include "common/data_chunk.hpp"

namespace cppcoldb {

// A Pipeline is a linear chain of operators that can be executed as a unit.
struct Pipeline {
    PhysicalOperator* source;                    // produces DataChunks
    std::vector<PhysicalOperator*> operators;    // transform in order
    PhysicalOperator* sink;                      // consumes final chunks

    // Pipelines this one depends on (must run first).
    std::vector<Pipeline*> dependencies;
};

// Per-operator local state allocated at pipeline init time.
// Operators may subclass this to hold scan cursors, partial aggregations, etc.
struct OperatorState {
    virtual ~OperatorState() = default;
};

// Per-scan-source local state.
struct ScanState : OperatorState {
    size_t current_row_group = 0;  // which RowGroup we are reading
    size_t row_offset        = 0;  // row offset within current RowGroup
};

} // namespace cppcoldb
```

```cpp
// src/execution/executor.hpp
#pragma once
#include <vector>
#include <memory>
#include "execution/pipeline.hpp"
#include "execution/physical_operator.hpp"

namespace cppcoldb {

class ClientContext;

// Holds the results of a query: column metadata and data chunks.
struct QueryResult {
    std::vector<std::string> column_names;
    std::vector<TypeId>      column_types;
    std::vector<DataChunk>   chunks;        // all result rows split into batches
    bool                     success = true;
    std::string              error_message;

    size_t RowCount() const;
};

class Executor {
public:
    explicit Executor(ClientContext& ctx);

    // Build pipelines from the physical plan.
    void Initialize(std::unique_ptr<PhysicalOperator> plan);

    // Execute all pipelines in order.
    void Execute();

    // Retrieve the accumulated result after Execute() completes.
    QueryResult GetResult();

private:
    // Recursively walk the physical plan and build Pipeline objects.
    void BuildPipelines(PhysicalOperator* op, Pipeline* current_pipeline);

    ClientContext&                              ctx_;
    std::unique_ptr<PhysicalOperator>           plan_;       // owns the tree
    std::vector<std::unique_ptr<Pipeline>>      pipelines_;
    std::unique_ptr<PhysicalResultCollector>    result_collector_;
};

} // namespace cppcoldb
```

```cpp
// src/execution/pipeline_executor.hpp
#pragma once
#include "execution/pipeline.hpp"
#include "common/data_chunk.hpp"

namespace cppcoldb {

class ClientContext;

class PipelineExecutor {
public:
    PipelineExecutor(ClientContext& ctx, Pipeline& pipeline);

    // Run the pipeline to completion (blocks until source is exhausted).
    void Execute();

private:
    // Flush any buffered operator state after source is exhausted.
    void TryFlushOperators();

    ClientContext& ctx_;
    Pipeline&      pipeline_;
    DataChunk      input_chunk_;    // reused each iteration
    DataChunk      output_chunk_;   // reused each iteration

    // Per-operator states, indexed parallel to pipeline.operators.
    std::vector<std::unique_ptr<OperatorState>> op_states_;
    std::unique_ptr<OperatorState>              source_state_;
    std::unique_ptr<OperatorState>              sink_state_;
};

} // namespace cppcoldb
```

## 6. Public Interfaces

```cpp
namespace cppcoldb {

// PhysicalOperator virtual interface (in physical_operator.hpp)
struct PhysicalOperator {
    // SOURCE role
    virtual void             InitScan(ScanState& state, ClientContext& ctx) {}
    virtual OperatorResultType GetData(ScanState& state, DataChunk& output,
                                       ClientContext& ctx) {
        return OperatorResultType::FINISHED;
    }

    // OPERATOR role
    virtual OperatorResultType Execute(const DataChunk& input, DataChunk& output,
                                       OperatorState& state, ClientContext& ctx) {
        return OperatorResultType::FINISHED;
    }

    // SINK role
    virtual void Consume(const DataChunk& input, OperatorState& state,
                         ClientContext& ctx) {}
    virtual void Finalize(OperatorState& state, ClientContext& ctx) {}

    // Called after source exhausted to flush any buffered operator output.
    virtual OperatorResultType TryFlush(DataChunk& output, OperatorState& state,
                                        ClientContext& ctx) {
        return OperatorResultType::FINISHED;
    }

    virtual ~PhysicalOperator() = default;
    OperatorRole role;
    std::vector<TypeId> output_types;
    std::vector<std::string> output_names;
    std::vector<std::unique_ptr<PhysicalOperator>> children;
};

// Executor public API
class Executor {
public:
    void        Initialize(std::unique_ptr<PhysicalOperator> plan);
    void        Execute();
    QueryResult GetResult();
};

} // namespace cppcoldb
```

## 7. Internal Algorithms

### Executor::BuildPipelines
```
BuildPipelines(op, current_pipeline):
  if op.role == SOURCE:
    current_pipeline.source = op
    // No children to recurse for a source

  else if op.role == OPERATOR:
    current_pipeline.operators.prepend(op)  // build list in top-down order
    // For HashJoinProbe: start a NEW pipeline for the build side
    if op is PhysicalHashJoinProbe:
      build_pipeline = new Pipeline()
      build_pipeline.sink = op.build_side
      BuildPipelines(op.build_side.child, build_pipeline)
      current_pipeline.dependencies.append(build_pipeline)
      pipelines_.append(build_pipeline)
    // Continue into the probe child (left/probe relation)
    BuildPipelines(op.children[0], current_pipeline)

  else if op.role == SINK:
    current_pipeline.sink = op
    BuildPipelines(op.children[0], current_pipeline)

  Time: O(physical_operators)
```

### PipelineExecutor::Execute
```
Execute():
  // --- Initialization ---
  source_state = pipeline.source.CreateScanState()
  pipeline.source.InitScan(source_state, ctx)
  for i, op in enumerate(pipeline.operators):
    op_states[i] = op.CreateOperatorState()
  sink_state = pipeline.sink.CreateSinkState()

  // --- Main loop ---
  while true:
    input_chunk.Reset()
    source_result = pipeline.source.GetData(source_state, input_chunk, ctx)

    if source_result == FINISHED and input_chunk.count == 0:
      // Source exhausted: flush any operators that buffer state (e.g. sort, aggregate)
      TryFlushOperators(sink_state)
      break

    // --- Operator chain ---
    // 'current_input' starts as the source chunk.
    // After each operator it becomes that operator's output.
    current_input = &input_chunk
    skip_to_next_source = false

    for i, op in enumerate(pipeline.operators):
      while true:  // inner loop handles HAVE_MORE_OUTPUT
        output_chunk.Reset()
        op_result = op.Execute(*current_input, output_chunk, op_states[i], ctx)

        if op_result == FINISHED:
          // e.g. PhysicalLimit hit its row cap — stop everything
          pipeline.sink.Finalize(sink_state, ctx)
          return   // ← done, do NOT fetch more from source

        if op_result == NEEDS_MORE_INPUT:
          // Operator buffered this chunk but has no output yet (e.g. HashJoinBuild).
          // Skip sink for this iteration; get next source chunk.
          skip_to_next_source = true
          break   // break inner while; break outer for-loop implicitly via flag

        // op_result == HAVE_MORE_OUTPUT or the chunk just has data
        // Pass output_chunk to the NEXT operator (or sink).
        if i + 1 < pipeline.operators.size():
          current_input = &output_chunk
          // If op_result == HAVE_MORE_OUTPUT: the NEXT operator will be called
          // with this output_chunk, then we loop back to call THIS operator again.
          // We achieve this by NOT breaking the inner while here for the next op.
          break   // move to next operator with output_chunk as its input
        else:
          // No more operators: send to sink
          if output_chunk.count > 0:
            pipeline.sink.Consume(output_chunk, sink_state, ctx)
          if op_result == HAVE_MORE_OUTPUT:
            continue   // call this last operator again with same current_input
          else:
            break      // HAVE_MORE_OUTPUT exhausted; get next source chunk

      if skip_to_next_source: break

  // --- Finalize ---
  pipeline.sink.Finalize(sink_state, ctx)
  Time: O(source_rows / VECTOR_SIZE * num_operators)
```

### TryFlushOperators
```
// Called after the source returns FINISHED.
// Some operators buffer state internally and only emit output when explicitly flushed.
// Examples: PhysicalHashAggregation emits all groups on flush,
//           PhysicalSort emits sorted output on flush.
TryFlushOperators(sink_state):
  for i, op in enumerate(pipeline.operators):
    while true:
      output_chunk.Reset()
      flush_result = op.TryFlush(output_chunk, op_states[i], ctx)
      // TryFlush returns HAVE_MORE_OUTPUT while there are more buffered rows,
      // and FINISHED when the buffer is drained.

      if output_chunk.count > 0:
        // Pass through remaining operators downstream of op
        current_input = &output_chunk
        for j in (i+1)..pipeline.operators.size():
          next_out.Reset()
          pipeline.operators[j].Execute(*current_input, next_out, op_states[j], ctx)
          current_input = &next_out
        if current_input->count > 0:
          pipeline.sink.Consume(*current_input, sink_state, ctx)

      if flush_result == FINISHED: break   // this operator fully drained
      // HAVE_MORE_OUTPUT: loop and flush again
```

### Concrete execution trace
```
Query: SELECT a FROM t WHERE a > 5
Table t has 3 rows: a=[3, 7, 2]
STANDARD_VECTOR_SIZE=1024; all 3 rows fit in one chunk.

Pipeline: PhysicalTableScan(source) → PhysicalFilter(op) → ResultCollector(sink)

Step 1: InitScan(scan_state)
  scan_state = {current_row_group=0, row_offset=0}

Step 2: Loop iteration 1
  input_chunk.Reset()                              // input_chunk.count = 0
  GetData(scan_state, input_chunk):
    load RowGroup 0, column 'a' → DataVector [3, 7, 2]
    apply MVCC filter (all rows visible)
    input_chunk.count = 3, input_chunk.columns[0] = [3, 7, 2]
    scan_state.row_offset = 3
    return HAVE_MORE_OUTPUT                        // chunk is non-empty but may have more

  // Operator chain
  current_input = &input_chunk                     // [3, 7, 2]
  i=0: PhysicalFilter.Execute(input=[3,7,2], output, state):
    Apply predicate a > 5 to each value:
      row 0: 3 > 5 = false → skip
      row 1: 7 > 5 = true  → keep → selection_vector[0] = 1
      row 2: 2 > 5 = false → skip
    output.count = 1, output.columns[0] = [7]
    return HAVE_MORE_OUTPUT

  // No more operators; send to sink:
  ResultCollector.Consume(output=[7], sink_state)
    → accumulates chunk [7] in result buffer

  // op_result was HAVE_MORE_OUTPUT; call Filter again with same input? No —
  // Filter is a stateless operator (it processed all rows and returned the kept
  // ones). HAVE_MORE_OUTPUT here just means "I produced output, keep going".
  // The executor fetches the next source chunk.

Step 3: Loop iteration 2
  input_chunk.Reset()
  GetData(scan_state, input_chunk):
    scan_state.row_offset=3 >= RowGroup.row_count=3 → no more rows
    input_chunk.count = 0
    return FINISHED

  // Source returned FINISHED with empty chunk
  TryFlushOperators(): PhysicalFilter has no buffered state → no-op
  break out of loop

Step 4: Finalize
  ResultCollector.Finalize(sink_state):
    result is ready: 1 row, column 'a', value [7]

Final QueryResult: column_names=["a"], chunks=[{count=1, col[0]=[7]}]
```

### Executor::Execute (pipeline ordering)
```
Execute():
  // Topological sort pipelines by dependency
  ordered = TopologicalSort(pipelines_)

  for pipeline in ordered:
    exec = PipelineExecutor(ctx_, pipeline)
    exec.Execute()   // throws on error; exception propagates out
```

## 8. Persistence Model

Not applicable. The executor is purely in-memory during query execution. Results are held in `QueryResult.chunks` until the caller reads them.

## 9. Concurrency Model

All query execution is single-threaded. The `Executor`, `Pipeline`, and `PipelineExecutor` objects are owned by a single `ClientContext` and are never shared across threads.

| Object | Thread Safety | Notes |
|--------|---------------|-------|
| `Executor` | Single-threaded | One per query, owned by ClientContext |
| `Pipeline` | Single-threaded | Built and run by the same thread |
| `DataChunk` | Single-threaded | Reused per loop iteration, not shared |

## 10. Configuration

```cpp
struct ExecutorConfig {
    // Maximum number of result DataChunks to buffer before returning to client.
    // 0 = unlimited.
    size_t max_result_chunks = 0;
};
```

## 11. Testing Strategy

- `TestExecuteSingleScan`: create a PhysicalTableScan over a 100-row table → QueryResult has 100 rows
- `TestExecuteFilterReducesRows`: PhysicalTableScan + PhysicalFilter(false predicate) → 0 rows
- `TestExecuteProjection`: select 1 column out of 3 → result has 1 column
- `TestExecuteLimit`: PhysicalLimit(n=10) on a 1000-row scan → exactly 10 rows in result
- `TestExecuteHashJoin`: two-pipeline hash join → correct joined rows with no duplicates
- `TestExecuteHashAggregation`: GROUP BY dept with SUM(salary) → one row per group
- `TestExecutePipelineOrdering`: build pipeline runs before probe pipeline
- `TestExecuteErrorPropagatetesRollback`: operator throws RuntimeError → Execute() throws, no result
- `TestExecuteResultCollector`: multiple chunks accumulated, RowCount() sums correctly
- `TestExecuteEmptyTable`: scan an empty table → QueryResult with 0 rows and correct schema

## 12. Open Questions

None.
