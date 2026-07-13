#include "cppcoldb/engine/execution/aggregate_hash_table.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::execution {

AggregateHashTable::AggregateHashTable(std::size_t num_aggregates, std::size_t initial_capacity)
    : num_aggregates_(num_aggregates), capacity_(initial_capacity) {}

AggEntry* AggregateHashTable::FindOrCreate(const std::vector<common::Value>& key) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void AggregateHashTable::ForEach(std::function<void(AggEntry&)> fn) { CPPCOLDB_NOT_IMPLEMENTED(); }

void AggregateHashTable::Resize() { CPPCOLDB_NOT_IMPLEMENTED(); }

void AggUpdate(AggregateState& state, AggFunc func, const common::DataVector& val_vec,
               std::size_t row_idx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

common::Value AggFinalize(const AggregateState& state, AggFunc func, common::TypeId result_type) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

common::Value VectorGetValue(const common::DataVector& vec, std::size_t i) { CPPCOLDB_NOT_IMPLEMENTED(); }

} // namespace cppcoldb::engine::execution
