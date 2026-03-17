#pragma once
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>
#include "common/types.hpp"
#include "planner/logical_plan/logical_plan.hpp"

namespace cppcoldb {

// Per-group accumulator for all aggregate functions.
struct AggregateState {
    int64_t count    = 0;
    double  sum      = 0.0;
    Value   min_val;
    Value   max_val;
    bool    min_set  = false;
    bool    max_set  = false;
};

// One group entry in the hash table.
struct AggEntry {
    std::vector<Value>          group_key;   // bound group-by values for this group
    std::vector<AggregateState> agg_states;  // one per aggregate expression
    AggEntry*                   next = nullptr; // collision chaining
};

// Hash table mapping group keys → AggEntry accumulators.
// Single-threaded; not thread-safe.
class AggregateHashTable {
public:
    explicit AggregateHashTable(size_t num_aggregates, size_t initial_capacity = 256);
    ~AggregateHashTable() = default;

    // Find or create the entry for the given group key.
    AggEntry* FindOrCreate(const std::vector<Value>& key);

    // Iterate all groups (for finalization / emit).
    void ForEach(std::function<void(AggEntry&)> fn);

    size_t EntryCount()                  const { return num_groups_; }
    const AggEntry& GetEntry(size_t i)   const { return *entry_pool_[i]; }

private:
    void Resize();

    size_t                                  num_aggregates_;
    size_t                                  capacity_;
    size_t                                  num_groups_ = 0;
    std::vector<AggEntry*>                  buckets_;
    std::vector<std::unique_ptr<AggEntry>>  entry_pool_;
};

// Update a single AggregateState for one row of data.
// val_vec: evaluated argument vector; row_idx: which row.
void AggUpdate(AggregateState& state, LogicalAggrExpr::AggFunc func,
               const DataVector& val_vec, size_t row_idx);

// Compute the final Value for an AggregateState.
Value AggFinalize(const AggregateState& state, LogicalAggrExpr::AggFunc func,
                  TypeId result_type);

// Extract a single Value from a DataVector at position i.
Value VectorGetValue(const DataVector& vec, size_t i);

} // namespace cppcoldb
