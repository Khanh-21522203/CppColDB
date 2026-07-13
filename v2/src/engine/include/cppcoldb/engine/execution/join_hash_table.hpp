#pragma once

#include <cstddef>
#include <unordered_map>
#include <vector>

#include "cppcoldb/common/types/data_chunk.hpp"
#include "cppcoldb/common/types/value.hpp"

namespace cppcoldb::engine::execution {

// In-memory build-side hash table for PhysicalHashJoinBuild / Probe: maps a
// join key to every matching build-side row, denormalized into (key, row)
// pairs so the probe side can emit matches without re-touching the source
// DataChunk.
class JoinHashTable {
public:
    struct Entry {
        std::vector<common::Value> key;
        std::vector<common::Value> row;
    };

    // Inserts row `row_idx` of `input` under the key formed by `key_col_idxs`.
    void Insert(const std::vector<std::size_t>& key_col_idxs, const common::DataChunk& input,
                std::size_t row_idx);

    const std::vector<Entry>& Probe(const std::vector<common::Value>& key) const;

    // Bulk-computes the hash of `key_col_idxs` for every row of `input` into
    // `hashes_out[0, row_count)`, for use with ProbeByHash.
    void ComputeHashes(const common::DataChunk& input, const std::vector<std::size_t>& key_col_idxs,
                        std::size_t* hashes_out, std::size_t row_count) const;

    const std::vector<Entry>& ProbeByHash(std::size_t hash) const;

private:
    static std::size_t HashKey(const std::vector<common::Value>& key);

    std::unordered_map<std::size_t, std::vector<Entry>> table_;
    static const std::vector<Entry>                     EMPTY;
};

} // namespace cppcoldb::engine::execution
