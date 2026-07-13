#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "cppcoldb/common/types/data_chunk.hpp"
#include "cppcoldb/common/types/type_id.hpp"
#include "cppcoldb/common/types/value.hpp"

namespace cppcoldb::engine::execution {

// Execution-owned aggregate function tag. The SQL binder/planner has its own
// higher-level aggregate-expression vocabulary; this enum is the physical
// layer's minimal, self-contained equivalent so execution does not need to
// depend on the sql module (see CONVENTIONS.md link-dependency table).
enum class AggFunc { COUNT, SUM, AVG, MIN, MAX };

// Running per-group accumulator for one aggregate expression.
struct AggregateState {
    std::int64_t  count = 0;
    double        sum = 0.0;
    common::Value min_val;
    common::Value max_val;
    bool          min_set = false;
    bool          max_set = false;
};

// One hash-table entry: a group's key plus one AggregateState per aggregate
// expression in the query. Entries chain via `next` for open-chaining
// collision resolution.
struct AggEntry {
    std::vector<common::Value>  group_key;
    std::vector<AggregateState> agg_states;
    AggEntry*                   next = nullptr;
};

// Open-addressing (chained) hash table backing PhysicalHashAggregation /
// PhysicalAggregationSource: one AggEntry per distinct group key.
class AggregateHashTable {
public:
    explicit AggregateHashTable(std::size_t num_aggregates, std::size_t initial_capacity = 256);
    ~AggregateHashTable() = default;

    AggEntry* FindOrCreate(const std::vector<common::Value>& key);
    void ForEach(std::function<void(AggEntry&)> fn);

    std::size_t EntryCount() const { return num_groups_; }
    const AggEntry& GetEntry(std::size_t i) const { return *entry_pool_[i]; }

private:
    void Resize();

    std::size_t                            num_aggregates_;
    std::size_t                            capacity_;
    std::size_t                            num_groups_ = 0;
    std::vector<AggEntry*>                 buckets_;
    std::vector<std::unique_ptr<AggEntry>> entry_pool_;
};

void AggUpdate(AggregateState& state, AggFunc func, const common::DataVector& val_vec,
               std::size_t row_idx);
common::Value AggFinalize(const AggregateState& state, AggFunc func, common::TypeId result_type);
common::Value VectorGetValue(const common::DataVector& vec, std::size_t i);

} // namespace cppcoldb::engine::execution
