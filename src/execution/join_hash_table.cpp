#include "execution/join_hash_table.hpp"
#include <cstring>
#include <functional>

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

const std::vector<JoinHashTable::Entry>&
JoinHashTable::ProbeByHash(size_t hash) const {
    auto it = table_.find(hash);
    if (it == table_.end()) return EMPTY;
    return it->second;
}

void JoinHashTable::ComputeHashes(const DataChunk& input,
                                   const std::vector<size_t>& key_col_idxs,
                                   size_t* hashes, size_t n) const {
    std::memset(hashes, 0, n * sizeof(size_t));
    for (size_t ki = 0; ki < key_col_idxs.size(); ++ki) {
        const DataVector& col = input.columns[key_col_idxs[ki]];
        switch (col.type) {
            case TypeId::BOOLEAN:
            case TypeId::INT8:
            case TypeId::INT16:
            case TypeId::INT32:
            case TypeId::INT64:
                for (size_t r = 0; r < n; ++r) {
                    size_t vh = col.IsNull(r) ? 0 : std::hash<int64_t>{}(col.int_data[r]);
                    hashes[r] ^= vh + 0x9e3779b9u + (hashes[r] << 6) + (hashes[r] >> 2);
                }
                break;
            case TypeId::FLOAT32:
            case TypeId::FLOAT64:
                for (size_t r = 0; r < n; ++r) {
                    size_t vh = col.IsNull(r) ? 0 : std::hash<double>{}(col.float_data[r]);
                    hashes[r] ^= vh + 0x9e3779b9u + (hashes[r] << 6) + (hashes[r] >> 2);
                }
                break;
            case TypeId::VARCHAR:
                for (size_t r = 0; r < n; ++r) {
                    size_t vh = col.IsNull(r) ? 0 : std::hash<std::string>{}(col.str_data[r]);
                    hashes[r] ^= vh + 0x9e3779b9u + (hashes[r] << 6) + (hashes[r] >> 2);
                }
                break;
            default:
                break;
        }
    }
}

} // namespace cppcoldb
