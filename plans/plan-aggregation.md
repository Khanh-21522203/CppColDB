# Feature: Aggregation

## 1. Purpose

Aggregation implements the GROUP BY and aggregate-function logic (COUNT, SUM, MIN, MAX, AVG). CppColDB uses hash aggregation: as input chunks are consumed, each row is mapped to a group key via hashing, and an `AggregateState` accumulator is updated for that group. After all input is consumed, the hash table is iterated to finalize and emit result chunks. Ungrouped aggregates (no GROUP BY) are a degenerate case treated as a single implicit group.

## 2. Responsibilities

- `PhysicalHashAggregation` (SINK): consume input DataChunks; for each row, compute the group key hash, find or create an `AggregateState` in the `AggregateHashTable`, and update the accumulators
- `AggregateHashTable`: map group keys → `AggregateState` objects; handle hash collisions via chaining
- `AggregateState`: per-group accumulator holding count, sum, min, max for each aggregate function
- Finalize step: iterate all groups; for AVG divide sum by count; emit output DataChunks
- Support aggregate functions: COUNT(*), COUNT(col), SUM, MIN, MAX, AVG
- Handle NULL inputs: NULL values are ignored by all aggregate functions except COUNT(*)

## 3. Non-Responsibilities

- Does not implement sorting or sort-based aggregation
- Does not implement DISTINCT within aggregates (e.g., COUNT(DISTINCT col))
- Does not implement window functions
- Does not implement HAVING filter (the Optimizer rewrites HAVING as a filter above the aggregate node)
- Does not spill to disk when the hash table exceeds memory

## 4. Architecture Design

```
Pipeline: Source → [Filter] → HashAggSink (SINK)

Input chunks flow into HashAggSink::Consume():
  Each row → hash(group_key_cols) → find bucket in AggregateHashTable
          → update AggregateState {count, sum, min, max}

After source exhausted, HashAggSink::Finalize():
  Iterate all (group_key, AggregateState) pairs in AggregateHashTable
  For each group:
    Compute final value (AVG = sum/count)
    Append (group_key_values..., agg_results...) to output DataChunk
    Emit output chunk when full (VECTOR_SIZE rows)

AggregateHashTable layout:
  buckets_[h] -> AggEntry{ key: vector<Value>,
                            state: AggregateState,
                            next: AggEntry* }
```

**Ungrouped aggregate**: when `group_keys` is empty, all rows map to the same dummy group (hash = 0, key = empty vector). The single `AggregateState` accumulates the entire table.

## 5. Core Data Structures (C++)

```cpp
// src/execution/aggregate_hash_table.hpp
#pragma once
#include <vector>
#include <memory>
#include <cstdint>
#include <limits>
#include "common/types.hpp"

namespace cppcoldb {

// Accumulator for a single aggregate function over one group.
struct AggregateState {
    int64_t count    = 0;
    double  sum      = 0.0;
    Value   min_val;         // initialized lazily on first non-NULL input
    Value   max_val;
    bool    min_set  = false;
    bool    max_set  = false;
};

// One entry in the aggregate hash table.
struct AggEntry {
    std::vector<Value>          key;      // group-by key values
    std::vector<AggregateState> states;   // one AggregateState per aggregate expr
    AggEntry*                   next;     // chaining for collision
};

// Hash table mapping group keys to aggregate accumulators.
class AggregateHashTable {
public:
    explicit AggregateHashTable(size_t num_aggregates,
                                size_t initial_capacity = 256);
    ~AggregateHashTable();

    // Find or create the AggEntry for the given key.
    AggEntry* FindOrCreate(const std::vector<Value>& key);

    // Iterator support for finalization.
    void ForEach(std::function<void(AggEntry&)> fn);

    size_t GroupCount() const { return num_groups_; }

private:
    void Resize();

    size_t                         num_aggregates_;
    size_t                         capacity_;
    size_t                         num_groups_;
    std::vector<AggEntry*>         buckets_;
    std::vector<std::unique_ptr<AggEntry>> entry_pool_;
};

} // namespace cppcoldb
```

```cpp
// src/execution/operator/hash_aggregation.hpp
#pragma once
#include "execution/physical_operator.hpp"
#include "execution/aggregate_hash_table.hpp"
#include "planner/logical_plan/logical_plan.hpp"

namespace cppcoldb {

enum class AggFunc { COUNT_STAR, COUNT, SUM, MIN, MAX, AVG };

struct AggFuncSpec {
    AggFunc                      func;
    bool                         is_star;   // COUNT(*)
    std::unique_ptr<LogicalExpr> arg;        // nullptr for COUNT(*)
    TypeId                       result_type;
};

struct HashAggState : OperatorState {
    std::unique_ptr<AggregateHashTable> ht;
};

struct PhysicalHashAggregation : PhysicalOperator {
    std::vector<std::unique_ptr<LogicalExpr>> group_keys;   // may be empty
    std::vector<AggFuncSpec>                  agg_specs;
    std::vector<std::string>                  output_names; // group cols + agg cols

    void Consume(const DataChunk& input, OperatorState& state,
                 ClientContext& ctx) override;
    void Finalize(OperatorState& state, ClientContext& ctx) override;
};

} // namespace cppcoldb
```

## 6. Public Interfaces

```cpp
namespace cppcoldb {

class AggregateHashTable {
public:
    explicit AggregateHashTable(size_t num_aggregates, size_t initial_capacity = 256);

    // Find or create the accumulator row for 'key'.
    AggEntry* FindOrCreate(const std::vector<Value>& key);

    // Iterate over all groups for finalization.
    void ForEach(std::function<void(AggEntry&)> fn);

    size_t GroupCount() const;
};

// Standalone aggregate update functions (called per row per aggregate).
void AggUpdate(AggregateState& state, AggFunc func, const Value& val);
Value AggFinalize(const AggregateState& state, AggFunc func);

} // namespace cppcoldb
```

## 7. Internal Algorithms

### HashAggSink::Consume
```
Consume(input_chunk, agg_state, ctx):
  ht = agg_state.ht
  for i in 0..input_chunk.count:
    // Build group key for row i
    key = []
    for expr in group_keys:
      key.append(ExprEvaluator::EvalScalar(expr, input_chunk, i))
    // If any group key component is NULL, this row is placed in the NULL group.

    entry = ht.FindOrCreate(key)

    for j, spec in enumerate(agg_specs):
      switch spec.func:
        COUNT_STAR:
          entry.states[j].count += 1

        COUNT:
          val = ExprEvaluator::EvalScalar(spec.arg, input_chunk, i)
          if not val.IsNull():
            entry.states[j].count += 1

        SUM:
          val = EvalScalar(spec.arg, input_chunk, i)
          if not val.IsNull():
            entry.states[j].sum   += val.GetFloat64()
            entry.states[j].count += 1  // for AVG denominator

        MIN:
          val = EvalScalar(spec.arg, input_chunk, i)
          if not val.IsNull():
            if not entry.states[j].min_set OR val < entry.states[j].min_val:
              entry.states[j].min_val = val
              entry.states[j].min_set = true

        MAX:
          // symmetric to MIN

        AVG:
          // same as SUM (sum += val, count += 1)

  Time: O(input.count * agg_specs.size())
```

### HashAggSink::Finalize (emit result chunks)
```
Finalize(agg_state, ctx):
  output_chunk = DataChunk()
  ht.ForEach(lambda entry:
    // Append group key values
    for k, key_val in enumerate(entry.key):
      output_chunk.columns[k].Append(key_val)

    // Finalize and append aggregate results
    for j, spec in enumerate(agg_specs):
      final_val = AggFinalize(entry.states[j], spec.func)
      output_chunk.columns[group_keys.size() + j].Append(final_val)

    output_chunk.count++

    if output_chunk.count == VECTOR_SIZE:
      EmitChunk(output_chunk)    // pass to next sink/operator
      output_chunk.Reset()
  )
  if output_chunk.count > 0:
    EmitChunk(output_chunk)   // emit final partial chunk
  Time: O(num_groups * agg_specs.size())
```

### AggFinalize
```
AggFinalize(state, func):
  switch func:
    COUNT_STAR, COUNT   -> Value::Integer(state.count)
    SUM                 -> Value::Float(state.sum)
    MIN                 -> state.min_val (or NULL if count == 0)
    MAX                 -> state.max_val
    AVG:
      if state.count == 0: return NULL
      return Value::Float(state.sum / state.count)
  Time: O(1)
```

### AggregateHashTable::FindOrCreate
```
FindOrCreate(key):
  h = HashValues(key) & (capacity - 1)
  entry = buckets[h]
  while entry != nullptr:
    if entry.key == key: return entry  // found existing group
    entry = entry.next

  // Create new group entry
  new_entry = AggEntry{key, states=[AggregateState() * num_aggregates], next=buckets[h]}
  buckets[h] = new_entry
  entry_pool.append(new_entry)
  num_groups++

  if num_groups > capacity * 0.75:
    Resize()   // double capacity, rehash

  return new_entry
  Time: O(1) amortized
```

## 8. Persistence Model

Not applicable. The aggregate hash table is an in-memory structure for the duration of a single query execution.

## 9. Concurrency Model

`PhysicalHashAggregation` is owned and executed by a single pipeline thread. The `AggregateHashTable` is not thread-safe; it is built and finalized by the same thread.

| Object | Lock | Usage |
|--------|------|-------|
| `AggregateHashTable` | None | Single-threaded build and finalize |
| `PhysicalHashAggregation` | None | Single-threaded sink |

## 10. Configuration

```cpp
struct AggregateConfig {
    size_t initial_hash_table_capacity = 256; // initial bucket count; doubles on resize
};
```

## 11. Testing Strategy

- `TestAggCountStar`: `SELECT COUNT(*) FROM t` on 500-row table → 1 row with value 500
- `TestAggCountColumn`: COUNT on a column with some NULLs → only non-NULL rows counted
- `TestAggSum`: SUM(amount) on 10 rows with known values → correct total
- `TestAggMin`: MIN on column with known min → correct minimum value
- `TestAggMax`: MAX on column with known max → correct maximum value
- `TestAggAvg`: AVG on 4 values {2, 4, 6, 8} → 5.0
- `TestAggAvgNullIgnored`: AVG where some inputs are NULL → NULLs excluded from count and sum
- `TestAggGroupBy`: `SELECT dept, COUNT(*) FROM t GROUP BY dept` → one row per distinct dept
- `TestAggGroupByMultiCol`: `SELECT a, b, SUM(c) FROM t GROUP BY a, b` → correct groups
- `TestAggUngrouped`: no GROUP BY → single output row
- `TestAggHashTableResize`: more groups than initial capacity → resize, all groups preserved
- `TestAggEmptyInput`: aggregate over empty table → for COUNT: 0; for SUM/MIN/MAX/AVG: NULL
- `TestAggOutputChunkBoundary`: more than 1024 groups → emitted across multiple output chunks

## 12. Open Questions

- HAVING clause: currently expected to be rewritten by the optimizer as a LogicalFilter above LogicalAggregate. If the optimizer does not do this, the aggregation operator would need to apply it during finalize.
- COUNT(DISTINCT col): deferred.
