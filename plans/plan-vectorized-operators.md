# Feature: Vectorized Operators

## 1. Purpose

Vectorized Operators are the concrete physical operator implementations that transform `DataChunk` batches inside pipelines. Instead of processing one row at a time, each operator receives a full batch of up to 1024 rows per call, applies its logic across entire column vectors, and produces an output batch. This approach enables compiler auto-vectorization (SIMD), reduces function-call overhead, and keeps column data in CPU cache. This feature covers `PhysicalTableScan`, `PhysicalFilter`, `PhysicalProjection`, `PhysicalLimit`, and `PhysicalResultCollector`.

## 2. Responsibilities

- `PhysicalTableScan`: read column data from the storage layer row-group by row-group; apply pushed-down filter predicates; emit DataChunks
- `PhysicalFilter`: evaluate a predicate expression over a column vector; build a selection vector; compact the passing rows into the output chunk
- `PhysicalProjection`: evaluate a list of expressions over the input chunk's column vectors; produce an output chunk with the projected columns
- `PhysicalLimit`: forward chunks from its child operator until `limit` rows have been emitted; signal `FINISHED` to stop the pipeline early
- `PhysicalResultCollector` (Sink): accumulate all incoming DataChunks into a `QueryResult`; return them via `GetResult()`

## 3. Non-Responsibilities

- Does not implement join logic (that is `plan-join-execution`)
- Does not implement aggregation (that is `plan-aggregation`)
- Does not implement INSERT/DELETE/UPDATE (those are mutation operators, separate files)
- Does not perform column decompression directly (calls `ColumnChunk::Scan()` which decompresses)
- Does not manage buffer pool memory

## 4. Architecture Design

```
Pipeline: Source → [Filter] → [Projection] → [Limit] → Sink(ResultCollector)

PhysicalTableScan (SOURCE)
  |  GetData() → decompressed DataChunk
  v
PhysicalFilter (OPERATOR)
  |  Execute() → compacted DataChunk (only passing rows)
  v
PhysicalProjection (OPERATOR)
  |  Execute() → DataChunk with derived/selected columns
  v
PhysicalLimit (OPERATOR)
  |  Execute() → DataChunk (truncated at limit boundary), FINISHED when done
  v
PhysicalResultCollector (SINK)
     Consume() → appends to result_chunks_
     Finalize() → no-op (data already accumulated)
```

**Selection vector**: `PhysicalFilter` builds a `std::vector<uint16_t>` of row indices that pass the predicate (max 1024 entries), then compacts the input chunk by those indices into the output chunk.

## 5. Core Data Structures (C++)

```cpp
// src/execution/operator/physical_table_scan.hpp
#pragma once
#include "execution/physical_operator.hpp"
#include "storage/column/row_group.hpp"

namespace cppcoldb {

class TableCatalogEntry;
class Transaction;

// Local scan state per pipeline execution.
struct TableScanState : ScanState {
    const TableCatalogEntry* table  = nullptr;
    size_t row_group_idx            = 0;    // which RowGroup we are on
    size_t row_offset_in_group      = 0;    // rows already read from current RG
};

struct PhysicalTableScan : PhysicalOperator {
    std::string          schema_name;
    std::string          table_name;
    std::vector<size_t>  column_ids;            // which columns to read
    std::vector<std::unique_ptr<LogicalExpr>> pushed_filters;

    void             InitScan(ScanState& state, ClientContext& ctx) override;
    OperatorResultType GetData(ScanState& state, DataChunk& output,
                               ClientContext& ctx) override;
};

} // namespace cppcoldb
```

```cpp
// src/execution/operator/physical_filter.hpp
#pragma once
#include "execution/physical_operator.hpp"

namespace cppcoldb {

struct PhysicalFilter : PhysicalOperator {
    std::unique_ptr<LogicalExpr> predicate;

    OperatorResultType Execute(const DataChunk& input, DataChunk& output,
                               OperatorState& state, ClientContext& ctx) override;
};

} // namespace cppcoldb
```

```cpp
// src/execution/operator/physical_projection.hpp
#pragma once
#include "execution/physical_operator.hpp"

namespace cppcoldb {

struct PhysicalProjection : PhysicalOperator {
    std::vector<std::unique_ptr<LogicalExpr>> exprs;

    OperatorResultType Execute(const DataChunk& input, DataChunk& output,
                               OperatorState& state, ClientContext& ctx) override;
};

} // namespace cppcoldb
```

```cpp
// src/execution/operator/physical_limit.hpp
#pragma once
#include "execution/physical_operator.hpp"

namespace cppcoldb {

struct LimitState : OperatorState {
    int64_t rows_emitted = 0;  // total rows sent to sink so far
    int64_t rows_skipped = 0;  // for OFFSET
};

struct PhysicalLimit : PhysicalOperator {
    int64_t limit;   // max rows to emit; -1 = no limit
    int64_t offset;  // rows to skip at start; 0 = no offset

    OperatorResultType Execute(const DataChunk& input, DataChunk& output,
                               OperatorState& state, ClientContext& ctx) override;
};

} // namespace cppcoldb
```

```cpp
// src/execution/operator/result_collector.hpp
#pragma once
#include "execution/physical_operator.hpp"

namespace cppcoldb {

struct ResultCollectorState : OperatorState {
    std::vector<DataChunk> chunks;  // accumulated result
};

struct PhysicalResultCollector : PhysicalOperator {
    void Consume(const DataChunk& input, OperatorState& state,
                 ClientContext& ctx) override;
    void Finalize(OperatorState& state, ClientContext& ctx) override;

    // Called by Executor::GetResult() after Execute() completes.
    QueryResult ExtractResult(OperatorState& state);
};

} // namespace cppcoldb
```

## 6. Public Interfaces

```cpp
namespace cppcoldb {

// ExprEvaluator evaluates a LogicalExpr over a DataChunk column-by-column.
// Used internally by Filter and Projection.
class ExprEvaluator {
public:
    // Evaluate expr for every row in chunk; write results into output DataVector.
    static void Evaluate(const LogicalExpr& expr, const DataChunk& chunk,
                         DataVector& output);

    // Evaluate a boolean predicate; return a selection vector of passing row indices.
    static std::vector<uint16_t> EvaluatePredicate(const LogicalExpr& pred,
                                                    const DataChunk& chunk);
};

// DataChunkCompact: produce a new chunk with only the rows at given indices.
void DataChunkCompact(const DataChunk& src,
                      const std::vector<uint16_t>& selection,
                      DataChunk& dst);

} // namespace cppcoldb
```

## 7. Internal Algorithms

### PhysicalTableScan::GetData
```
GetData(scan_state, output, ctx):
  table = ctx.catalog.GetEntry(schema_name, table_name, ctx.transaction)
  row_groups = table.storage.GetRowGroups()

  while scan_state.row_group_idx < row_groups.size():
    rg = row_groups[scan_state.row_group_idx]

    // Zone-map check: skip RowGroup if pushed filter can never match
    if pushed_filters not empty:
      if rg.ZoneMapExcludes(pushed_filters):
        scan_state.row_group_idx++
        scan_state.row_offset_in_group = 0
        continue

    // Read up to STANDARD_VECTOR_SIZE rows from this RowGroup
    rows_read = rg.Scan(scan_state.row_offset_in_group, column_ids, output, ctx.transaction)
    scan_state.row_offset_in_group += rows_read

    if scan_state.row_offset_in_group >= rg.row_count:
      scan_state.row_group_idx++
      scan_state.row_offset_in_group = 0

    if output.count > 0:
      return HAVE_MORE_OUTPUT

  return FINISHED
  Time per call: O(VECTOR_SIZE * columns_read)
```

### PhysicalFilter::Execute
```
Execute(input, output, state, ctx):
  1. selection = ExprEvaluator::EvaluatePredicate(predicate, input)
     // selection[i] = row index in input that passes the predicate
     // Time: O(input.count) per column referenced

  2. if selection is empty:
       return NEEDS_MORE_INPUT  // no rows passed; request next source chunk

  3. DataChunkCompact(input, selection, output)
     // Copy only selected rows; each column vector is compacted
     // Time: O(input.count * columns)

  4. return HAVE_MORE_OUTPUT

EvaluatePredicate for BinaryOp (e.g. col > literal):
  for i in 0..input.count:
    if validity[i] is null: selection skip i
    else: eval op(col[i], literal) -> include i if true
  Time: O(n) vectorizable loop
```

### PhysicalProjection::Execute
```
Execute(input, output, state, ctx):
  output.count = input.count
  for i, expr in enumerate(exprs):
    ExprEvaluator::Evaluate(expr, input, output.columns[i])
  return HAVE_MORE_OUTPUT

ExprEvaluator::Evaluate for BinaryOp (e.g. a + b):
  left_vec  = Evaluate(left, input)
  right_vec = Evaluate(right, input)
  for i in 0..input.count:
    if left_vec.IsNull(i) or right_vec.IsNull(i):
      output.SetNull(i)
    else:
      output.int_data[i] = left_vec.int_data[i] + right_vec.int_data[i]
  Time: O(n) vectorizable loop
```

### PhysicalLimit::Execute
```
Execute(input, output, limit_state, ctx):
  // Handle OFFSET first
  to_skip = max(0, limit - limit_state.rows_skipped)
  if to_skip > 0:
    skip = min(input.count, to_skip)
    DataChunkSlice(output, input, rows[skip..input.count])
    limit_state.rows_skipped += skip
  else:
    output = input  // no offset, pass through

  // Apply LIMIT
  remaining = limit - limit_state.rows_emitted
  if output.count > remaining:
    output.count = remaining  // truncate to remaining allowed rows

  limit_state.rows_emitted += output.count

  if limit_state.rows_emitted >= limit:
    return FINISHED  // signal pipeline to stop early

  return HAVE_MORE_OUTPUT
  Time: O(output.count)
```

## 8. Persistence Model

Not applicable. All operator logic is in-memory during execution.

## 9. Concurrency Model

All operators in a pipeline are owned and executed by a single thread. No locks needed within operators. `ClientContext` and `Transaction` are per-session and not shared.

## 10. Configuration

```cpp
// STANDARD_VECTOR_SIZE (from core types) controls batch size: 1024 rows.
// No per-operator runtime configuration.
```

## 11. Testing Strategy

- `TestTableScanAllRows`: scan a 5000-row table with no filter → 5000 rows emitted total
- `TestTableScanZoneMapSkip`: push a filter that excludes some RowGroups → skipped RowGroups not read
- `TestTableScanColumnPruning`: request 2 of 5 columns → output chunk has 2 columns
- `TestTableScanMVCC`: rows inserted by a non-visible transaction are excluded
- `TestFilterPredicate`: 100-row chunk, predicate matches 30 → output.count == 30
- `TestFilterAllPass`: predicate is always true → output identical to input
- `TestFilterNonePass`: predicate is always false → NEEDS_MORE_INPUT returned
- `TestFilterNullHandling`: NULL rows are excluded by the predicate
- `TestProjectionColumnRef`: select col[1] → output has only col[1] values
- `TestProjectionArithmetic`: `a + b` expression → correct vectorized sum
- `TestProjectionNullPropagation`: NULL input → NULL output for arithmetic exprs
- `TestLimitBasic`: limit=10 on 1000-row scan → exactly 10 rows
- `TestLimitOffset`: limit=5, offset=10 → rows 10–14
- `TestLimitFinishedSignal`: when rows_emitted reaches limit, returns FINISHED
- `TestResultCollectorAccumulates`: feed 5 chunks → GetResult has all rows
- `TestResultCollectorEmpty`: no chunks consumed → empty QueryResult

## 12. Open Questions

- `ExprEvaluator` for string comparisons (LIKE, IN): basic equality and comparison are in scope; LIKE pattern matching is deferred.
- ORDER BY sorting operator: deferred to a future plan (would be a separate `PhysicalSort` operator).
