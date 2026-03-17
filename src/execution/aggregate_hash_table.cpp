#include "execution/aggregate_hash_table.hpp"
#include "common/exception.hpp"
#include <cstring>

namespace cppcoldb {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static size_t HashGroupKey(const std::vector<Value>& key) {
    size_t h = 0;
    for (const auto& v : key) {
        // Combine hashes using the Fibonacci mixing constant.
        h ^= ValueHash(v) + 0x9e3779b9u + (h << 6) + (h >> 2);
    }
    return h;
}

Value VectorGetValue(const DataVector& vec, size_t i) {
    if (vec.IsNull(i)) return Value::Null(vec.type);
    switch (vec.type) {
        case TypeId::BOOLEAN:
            return Value::Boolean(vec.int_data[i] != 0);
        case TypeId::INT8:
        case TypeId::INT16:
        case TypeId::INT32:
        case TypeId::INT64:
            return Value{vec.type, vec.int_data[i]};
        case TypeId::FLOAT32:
        case TypeId::FLOAT64:
            return Value{vec.type, vec.float_data[i]};
        case TypeId::VARCHAR:
            return Value::Varchar(vec.str_data[i]);
        default:
            return Value::Null();
    }
}

// ---------------------------------------------------------------------------
// AggregateHashTable
// ---------------------------------------------------------------------------

AggregateHashTable::AggregateHashTable(size_t num_aggregates, size_t initial_capacity)
    : num_aggregates_(num_aggregates),
      capacity_(initial_capacity),
      buckets_(initial_capacity, nullptr) {}

AggEntry* AggregateHashTable::FindOrCreate(const std::vector<Value>& key) {
    size_t h = HashGroupKey(key) & (capacity_ - 1);
    AggEntry* e = buckets_[h];
    while (e) {
        if (e->group_key == key) return e;
        e = e->next;
    }

    // Create new entry.
    auto entry = std::make_unique<AggEntry>();
    entry->group_key  = key;
    entry->agg_states.resize(num_aggregates_);
    entry->next       = buckets_[h];
    AggEntry* raw     = entry.get();
    buckets_[h]       = raw;
    entry_pool_.push_back(std::move(entry));
    ++num_groups_;

    if (num_groups_ > capacity_ * 3 / 4) {
        Resize();
        // raw is still valid — owned by entry_pool_
    }
    return raw;
}

void AggregateHashTable::Resize() {
    capacity_ *= 2;
    buckets_.assign(capacity_, nullptr);
    for (auto& owned : entry_pool_) {
        size_t h = HashGroupKey(owned->group_key) & (capacity_ - 1);
        owned->next = buckets_[h];
        buckets_[h] = owned.get();
    }
}

void AggregateHashTable::ForEach(std::function<void(AggEntry&)> fn) {
    for (auto& owned : entry_pool_) {
        fn(*owned);
    }
}

// ---------------------------------------------------------------------------
// AggUpdate
// ---------------------------------------------------------------------------

void AggUpdate(AggregateState& state, LogicalAggrExpr::AggFunc func,
               const DataVector& val_vec, size_t row_idx) {
    switch (func) {
        case LogicalAggrExpr::AggFunc::COUNT:
            if (!val_vec.IsNull(row_idx)) ++state.count;
            break;

        case LogicalAggrExpr::AggFunc::SUM:
        case LogicalAggrExpr::AggFunc::AVG: {
            if (!val_vec.IsNull(row_idx)) {
                double v = (val_vec.type == TypeId::FLOAT64 || val_vec.type == TypeId::FLOAT32)
                           ? val_vec.float_data[row_idx]
                           : static_cast<double>(val_vec.int_data[row_idx]);
                state.sum += v;
                ++state.count;
            }
            break;
        }

        case LogicalAggrExpr::AggFunc::MIN: {
            if (!val_vec.IsNull(row_idx)) {
                Value v = VectorGetValue(val_vec, row_idx);
                if (!state.min_set || v < state.min_val) {
                    state.min_val = v;
                    state.min_set = true;
                }
                ++state.count;
            }
            break;
        }

        case LogicalAggrExpr::AggFunc::MAX: {
            if (!val_vec.IsNull(row_idx)) {
                Value v = VectorGetValue(val_vec, row_idx);
                if (!state.max_set || state.max_val < v) {
                    state.max_val = v;
                    state.max_set = true;
                }
                ++state.count;
            }
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// AggFinalize
// ---------------------------------------------------------------------------

Value AggFinalize(const AggregateState& state, LogicalAggrExpr::AggFunc func,
                  TypeId result_type) {
    switch (func) {
        case LogicalAggrExpr::AggFunc::COUNT:
            return Value::Integer(state.count);

        case LogicalAggrExpr::AggFunc::SUM:
            if (state.count == 0) return Value::Null(result_type);
            if (result_type == TypeId::FLOAT64 || result_type == TypeId::FLOAT32)
                return Value::Float(state.sum);
            return Value::Integer(static_cast<int64_t>(state.sum));

        case LogicalAggrExpr::AggFunc::AVG:
            if (state.count == 0) return Value::Null(TypeId::FLOAT64);
            return Value::Float(state.sum / static_cast<double>(state.count));

        case LogicalAggrExpr::AggFunc::MIN:
            if (!state.min_set) return Value::Null(result_type);
            return state.min_val;

        case LogicalAggrExpr::AggFunc::MAX:
            if (!state.max_set) return Value::Null(result_type);
            return state.max_val;
    }
    return Value::Null();
}

} // namespace cppcoldb
