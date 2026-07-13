#include "cppcoldb/engine/execution/join_hash_table.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::execution {

const std::vector<JoinHashTable::Entry> JoinHashTable::EMPTY;

void JoinHashTable::Insert(const std::vector<std::size_t>& key_col_idxs, const common::DataChunk& input,
                            std::size_t row_idx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

const std::vector<JoinHashTable::Entry>& JoinHashTable::Probe(const std::vector<common::Value>& key) const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void JoinHashTable::ComputeHashes(const common::DataChunk& input,
                                   const std::vector<std::size_t>& key_col_idxs, std::size_t* hashes_out,
                                   std::size_t row_count) const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

const std::vector<JoinHashTable::Entry>& JoinHashTable::ProbeByHash(std::size_t hash) const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

std::size_t JoinHashTable::HashKey(const std::vector<common::Value>& key) { CPPCOLDB_NOT_IMPLEMENTED(); }

} // namespace cppcoldb::engine::execution
