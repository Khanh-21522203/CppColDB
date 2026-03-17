#include "execution/join_hash_table.hpp"

namespace cppcoldb {

const std::vector<JoinHashTable::Entry> JoinHashTable::EMPTY;

// Extract a Value from a DataVector at the given row index.
static Value ExtractValue(const DataVector& vec, size_t idx) {
    if (vec.IsNull(idx)) return Value::Null(vec.type);
    switch (vec.type) {
        case TypeId::BOOLEAN:
            return Value::Boolean(vec.int_data[idx] != 0);
        case TypeId::INT8:
        case TypeId::INT16:
        case TypeId::INT32:
        case TypeId::INT64:
            return Value{vec.type, vec.int_data[idx]};
        case TypeId::FLOAT32:
        case TypeId::FLOAT64:
            return Value{vec.type, vec.float_data[idx]};
        case TypeId::VARCHAR:
            return Value{vec.type, vec.str_data[idx]};
        default:
            return Value::Null(vec.type);
    }
}

size_t JoinHashTable::HashKey(const std::vector<Value>& key) {
    size_t h = 0;
    for (const auto& v : key) {
        h ^= ValueHash(v) + 0x9e3779b9u + (h << 6) + (h >> 2);
    }
    return h;
}

void JoinHashTable::Insert(const std::vector<size_t>& key_col_idxs,
                           const DataChunk& input, size_t row_idx) {
    Entry e;
    for (size_t col : key_col_idxs) {
        e.key.push_back(ExtractValue(input.columns[col], row_idx));
    }
    for (size_t col = 0; col < input.columns.size(); ++col) {
        e.row.push_back(ExtractValue(input.columns[col], row_idx));
    }
    size_t h = HashKey(e.key);
    table_[h].push_back(std::move(e));
}

const std::vector<JoinHashTable::Entry>&
JoinHashTable::Probe(const std::vector<Value>& key) const {
    size_t h = HashKey(key);
    auto it = table_.find(h);
    if (it == table_.end()) return EMPTY;
    return it->second;
}

} // namespace cppcoldb
