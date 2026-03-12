# Feature: Join Execution

## 1. Purpose

Join Execution implements the runtime logic for combining rows from two relations. The primary algorithm is hash join (build phase + probe phase), which runs as two separate pipelines: the first builds a hash table from the right (inner/build) relation, and the second probes that hash table with rows from the left (outer/probe) relation. A nested-loop join is provided as a fallback for non-equi conditions and very small relations.

## 2. Responsibilities

- `PhysicalHashJoinBuild` (SINK): consume the build relation's DataChunks; insert each row into a `HashTable` keyed on the join key columns
- `PhysicalHashJoinProbe` (OPERATOR): for each probe relation chunk, hash the key columns, look up matches in the pre-built `HashTable`, and emit combined output rows
- Handle hash collisions correctly via chaining within each bucket
- Support INNER JOIN (emit only matching rows) and LEFT JOIN (emit NULL-filled right side for non-matching left rows)
- `PhysicalNestedLoopJoin` (OPERATOR): for each row in the outer (left) relation, scan the full inner (right) relation and emit rows where the join condition holds; used as fallback for non-equi or CROSS joins
- `HashTable`: the in-memory hash table used by the hash join operators; supports `Insert()` and `Probe()` operations on `DataChunk` keys

## 3. Non-Responsibilities

- Does not implement sort-merge join
- Does not handle out-of-core (disk-spilling) hash joins
- Does not choose the join algorithm (that is the Physical Planner's responsibility)
- Does not perform predicate pushdown into join conditions (that is the Optimizer)
- Does not manage transaction visibility directly (the scan operators handle MVCC filtering before rows reach the join)

## 4. Architecture Design

```
Hash Join — Two Pipelines:

Pipeline 1 (Build):
  PhysicalTableScan (right relation) → [Filter] → PhysicalHashJoinBuild (SINK)
        |
        | Builds HashTable in memory
        |
        v
    HashTable{key_cols → [row_data, ...]}

Pipeline 2 (Probe) [depends on Pipeline 1]:
  PhysicalTableScan (left relation) → [Filter] → PhysicalHashJoinProbe (OPERATOR)
        |                                                    |
        |                                         Probes HashTable
        |                                                    |
        v                                                    v
  PhysicalResultCollector (SINK) ← combined rows (probe cols + build cols)

Nested-Loop Join — Single Pipeline:
  PhysicalTableScan (left) → PhysicalNestedLoopJoin (OPERATOR) → Sink
                                    |
                              Internally rescans
                              right relation for each left row
```

**Hash collision handling**: each bucket in the `HashTable` holds a linked list of `RowEntry` nodes. During probe, all entries in the bucket are checked for full key equality (not just hash equality).

## 5. Core Data Structures (C++)

```cpp
// src/execution/hash_table.hpp
#pragma once
#include <vector>
#include <cstdint>
#include <memory>
#include "common/types.hpp"
#include "common/data_chunk.hpp"

namespace cppcoldb {

// One row stored in the hash table.
struct HashEntry {
    std::vector<Value> key;    // join key values (for equality re-check)
    std::vector<Value> row;    // full row values (all columns of build side)
    HashEntry*         next;   // chaining for collision
};

// In-memory hash table for hash join.
// Not thread-safe; owned by a single pipeline.
class HashTable {
public:
    explicit HashTable(size_t initial_capacity = 1024);
    ~HashTable();

    // Insert all rows from chunk using the columns at key_col_indices as the key.
    void Insert(const DataChunk& chunk, const std::vector<size_t>& key_col_indices);

    // Probe: for each row in probe_chunk (using probe_key_col_indices as key),
    // find all matching build-side rows. Calls match_cb for each (probe_row, build_row) pair.
    // Returns number of matches found.
    using MatchCallback = std::function<void(size_t probe_row_idx, const HashEntry& match)>;
    void Probe(const DataChunk& probe_chunk,
               const std::vector<size_t>& probe_key_col_indices,
               MatchCallback match_cb) const;

    size_t Size() const { return num_entries_; }

private:
    static constexpr double LOAD_FACTOR = 0.75;

    void Resize();

    std::vector<HashEntry*> buckets_;    // open array of bucket heads
    size_t                  capacity_;   // number of buckets (power of 2)
    size_t                  num_entries_;
    std::vector<std::unique_ptr<HashEntry>> entry_pool_; // owns all HashEntry objects
};

} // namespace cppcoldb
```

```cpp
// src/execution/operator/physical_hash_join.hpp
#pragma once
#include "execution/physical_operator.hpp"
#include "execution/hash_table.hpp"

namespace cppcoldb {

// Sink: builds the hash table.
struct HashJoinBuildState : OperatorState {
    std::unique_ptr<HashTable> hash_table;
};

struct PhysicalHashJoinBuild : PhysicalOperator {
    std::vector<size_t> build_key_cols;  // column indices in the build DataChunk

    void Consume(const DataChunk& input, OperatorState& state,
                 ClientContext& ctx) override;
    void Finalize(OperatorState& state, ClientContext& ctx) override;

    // After finalize: provides the built hash table to the probe operator.
    HashTable* GetHashTable(OperatorState& state);
};

// Operator: probes the hash table.
struct HashJoinProbeState : OperatorState {
    HashTable* hash_table = nullptr;  // pointer to build-side's hash table
    // Buffer for matches that overflow a single output chunk.
    std::vector<std::pair<size_t, const HashEntry*>> pending_matches;
    size_t pending_idx = 0;
};

struct PhysicalHashJoinProbe : PhysicalOperator {
    enum class JoinType { INNER, LEFT };
    JoinType                                  join_type = JoinType::INNER;
    std::shared_ptr<PhysicalHashJoinBuild>    build_side;
    std::vector<size_t>                       probe_key_cols;

    OperatorResultType Execute(const DataChunk& input, DataChunk& output,
                               OperatorState& state, ClientContext& ctx) override;
};

} // namespace cppcoldb
```

```cpp
// src/execution/operator/physical_nested_loop_join.hpp
#pragma once
#include "execution/physical_operator.hpp"

namespace cppcoldb {

struct NLJState : OperatorState {
    // Buffered copy of the entire right (inner) relation.
    // Loaded once during the first Execute() call.
    std::vector<DataChunk> inner_chunks;
    bool                   inner_loaded = false;
};

struct PhysicalNestedLoopJoin : PhysicalOperator {
    std::unique_ptr<LogicalExpr> condition; // nullptr for CROSS JOIN

    OperatorResultType Execute(const DataChunk& input, DataChunk& output,
                               OperatorState& state, ClientContext& ctx) override;
};

} // namespace cppcoldb
```

## 6. Public Interfaces

```cpp
namespace cppcoldb {

class HashTable {
public:
    explicit HashTable(size_t initial_capacity = 1024);

    void Insert(const DataChunk& chunk, const std::vector<size_t>& key_col_indices);

    void Probe(const DataChunk& probe_chunk,
               const std::vector<size_t>& probe_key_col_indices,
               MatchCallback match_cb) const;

    size_t Size() const;
};

} // namespace cppcoldb
```

## 7. Internal Algorithms

### Hash Join Build Phase
```
HashJoinBuild::Consume(input_chunk, build_state):
  hash_table = build_state.hash_table
  for i in 0..input_chunk.count:
    key = [input_chunk.columns[k][i] for k in build_key_cols]
    row = [input_chunk.columns[c][i] for c in all_columns]
    h   = HashValues(key) & (capacity - 1)
    entry = new HashEntry{key, row, next=buckets[h]}
    buckets[h] = entry
    num_entries++
    if num_entries > capacity * LOAD_FACTOR:
      Resize()  // double capacity, rehash all entries
  Time: O(input.count) amortized

HashJoinBuild::Finalize(build_state):
  // Hash table is complete; no action needed.
  // ProbeState will be wired to point at this hash table by the Executor.
```

### Hash Join Probe Phase
```
HashJoinProbe::Execute(input_chunk, output_chunk, probe_state):
  // First: drain any pending matches from previous call (output_chunk overflow)
  if probe_state.pending_idx < probe_state.pending_matches.size():
    EmitMatches(output_chunk, probe_state, input_chunk)
    if output_chunk.count == VECTOR_SIZE: return HAVE_MORE_OUTPUT

  ht = probe_state.hash_table
  for i in 0..input_chunk.count:
    probe_key = [input_chunk.columns[k][i] for k in probe_key_cols]
    h = HashValues(probe_key) & (ht.capacity - 1)
    entry = ht.buckets[h]
    found = false
    while entry != nullptr:
      if entry.key == probe_key:  // full key comparison
        probe_state.pending_matches.push_back({i, entry})
        found = true
      entry = entry.next

    if not found AND join_type == LEFT:
      // Emit left row with NULLs for right side
      EmitLeftNullRow(output_chunk, input_chunk, i)

    if output_chunk.count >= VECTOR_SIZE:
      return HAVE_MORE_OUTPUT

  return NEEDS_MORE_INPUT  // consumed all of input_chunk

EmitMatches(output_chunk, pending, input_chunk):
  while pending.pending_idx < pending.pending_matches.size()
        AND output_chunk.count < VECTOR_SIZE:
    (probe_row, match) = pending.pending_matches[pending.pending_idx]
    // Append probe row columns + match.row columns to output_chunk
    output_chunk.count++
    pending.pending_idx++
  Time per call: O(VECTOR_SIZE)
```

### HashTable::Resize
```
Resize():
  new_capacity = capacity * 2
  new_buckets = vector<HashEntry*>(new_capacity, nullptr)
  for entry in all_entries (via entry_pool_):
    h = HashValues(entry.key) & (new_capacity - 1)
    entry.next = new_buckets[h]
    new_buckets[h] = entry
  capacity = new_capacity
  buckets = new_buckets
  Time: O(num_entries)
```

### Nested-Loop Join
```
NLJExecute(input_chunk, output_chunk, nlj_state, ctx):
  if not nlj_state.inner_loaded:
    // Load entire right (inner) relation into memory
    inner_source = children[1]  // right child source
    inner_state  = inner_source.CreateScanState()
    inner_source.InitScan(inner_state, ctx)
    loop:
      inner_chunk = DataChunk()
      result = inner_source.GetData(inner_state, inner_chunk, ctx)
      nlj_state.inner_chunks.push_back(inner_chunk)
      if result == FINISHED: break
    nlj_state.inner_loaded = true

  for i in 0..input_chunk.count:            // outer loop
    for inner_chunk in nlj_state.inner_chunks:
      for j in 0..inner_chunk.count:        // inner loop
        if condition is nullptr             // CROSS JOIN
           OR EvalCondition(i, input_chunk, j, inner_chunk):
          AppendCombinedRow(output_chunk, input_chunk, i, inner_chunk, j)
          if output_chunk.count == VECTOR_SIZE:
            return HAVE_MORE_OUTPUT

  return NEEDS_MORE_INPUT
  Time: O(outer.count * inner_total_rows) per call
```

## 8. Persistence Model

Not applicable. The hash table is an in-memory structure for the duration of a single query execution.

## 9. Concurrency Model

All join operators are owned and executed by a single thread. The `HashTable` is not thread-safe; it is built by the build pipeline and then read-only during the probe pipeline. The `Executor` guarantees the build pipeline completes before the probe pipeline starts.

| Object | Lock | Usage |
|--------|------|-------|
| `HashTable` | None | Write during build pipeline; read-only during probe |
| `PhysicalHashJoinBuild` | None | Single-threaded sink |
| `PhysicalHashJoinProbe` | None | Single-threaded operator |

## 10. Configuration

```cpp
struct HashJoinConfig {
    size_t initial_hash_table_capacity = 1024; // buckets; doubles on resize
};
```

## 11. Testing Strategy

- `TestHashTableInsertProbe`: insert 100 rows with unique keys → probe each key finds exactly 1 match
- `TestHashTableCollision`: insert rows with identical hash → all found via chaining
- `TestHashTableResize`: insert beyond load factor threshold → size grows, all entries still findable
- `TestHashJoinInner`: join two 100-row tables on equal key → only matching rows emitted
- `TestHashJoinLeftNullFill`: LEFT JOIN with non-matching left rows → right cols are NULL
- `TestHashJoinOutputChunkBoundary`: matches span multiple output chunks → all matches emitted exactly once
- `TestHashJoinDuplicateKeys`: build side has duplicate keys → all duplicates emitted in probe
- `TestHashJoinEmptyBuildSide`: build side is empty → probe emits nothing (INNER) or all-NULL (LEFT)
- `TestNestedLoopJoinCross`: CROSS JOIN 3×4 rows → 12 output rows
- `TestNestedLoopJoinCondition`: equi join via NLJ → same results as hash join
- `TestNestedLoopJoinEmptyInner`: right side empty → 0 output rows

## 12. Open Questions

- Out-of-core hash join: for build sides that exceed available memory, a disk-spilling strategy (partitioned hash join) would be needed. Deferred for this educational project.
- RIGHT JOIN and FULL OUTER JOIN: only INNER and LEFT are in scope for initial implementation.
