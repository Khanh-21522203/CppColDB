#pragma once
#include "common/types.hpp"
#include "storage/scan_predicate.hpp"
#include <vector>
#include <string>
#include <numeric>

namespace cppcoldb {

enum class PartitionType : uint8_t {
    NONE  = 0,
    RANGE = 1,
    HASH  = 2,
    LIST  = 3,
};

// One logical partition definition.
// For RANGE: upper_bounds[k] is the exclusive upper bound for the k-th key column.
//            upper_bounds.empty() = open upper bound (+inf) for the last partition.
// For LIST:  list_values[j][k] = k-th key value of the j-th value tuple for this partition.
// For HASH:  defs is empty (routing is combined hash(keys) % num_partitions).
struct PartitionDef {
    std::vector<Value>              upper_bounds; // RANGE: one per key col; empty = open upper bound
    std::vector<std::vector<Value>> list_values;  // LIST: outer=tuples, inner=key cols
};

struct PartitionInfo {
    PartitionType            type              = PartitionType::NONE;
    std::vector<std::string> partition_cols;                   // key column names (1+ for composite)
    std::vector<int>         partition_col_idxs;               // resolved indices into TableCatalogEntry::columns
    uint32_t                 num_partitions    = 0;
    std::vector<PartitionDef> defs;           // RANGE: N entries; LIST: N entries; HASH: empty

    bool IsPartitioned() const { return type != PartitionType::NONE; }

    // Route composite partition key to a partition id [0, num_partitions).
    // nkeys must equal partition_cols.size().
    uint32_t RouteRow(const Value* keys, size_t nkeys) const;

    // Single-key convenience wrapper — for backward compat and single-column partitions.
    uint32_t RouteRow(const Value& key) const { return RouteRow(&key, 1); }

    // Return the subset of partition ids that cannot be excluded by predicates.
    // predicates must use table-column indices (not chunk positions).
    // For composite keys, only the leading key column's predicates are used for pruning.
    std::vector<uint32_t> PrunedPartitions(
        const std::vector<ScanPredicate>& predicates) const;
};

} // namespace cppcoldb
