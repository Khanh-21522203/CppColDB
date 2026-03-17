#pragma once
#include <vector>
#include <unordered_map>
#include "common/types.hpp"

namespace cppcoldb {

// Hash table used by the hash join operators.
// Maps multi-column join keys → lists of matching right-side row tuples.
class JoinHashTable {
public:
    struct Entry {
        std::vector<Value> key; // join key values
        std::vector<Value> row; // all right-side column values
    };

    // Insert a single row from the right-side input chunk.
    // key_col_idxs: positions within `input` that form the join key.
    void Insert(const std::vector<size_t>& key_col_idxs,
                const DataChunk& input, size_t row_idx);

    // Returns all entries matching key (empty vector if none).
    const std::vector<Entry>& Probe(const std::vector<Value>& key) const;

private:
    std::unordered_map<size_t, std::vector<Entry>> table_;
    static const std::vector<Entry>                EMPTY;
    static size_t HashKey(const std::vector<Value>& key);
};

} // namespace cppcoldb
