#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cppcoldb/common/types/column_id.hpp"
#include "cppcoldb/common/types/value.hpp"
#include "cppcoldb/engine/storage/scan_predicate.hpp"

namespace cppcoldb::engine::storage {

enum class PartitionType : std::uint8_t {
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
    std::vector<common::Value>              upper_bounds;
    std::vector<std::vector<common::Value>> list_values;
};

struct PartitionInfo {
    PartitionType                  type           = PartitionType::NONE;
    std::vector<std::string>       partition_cols;      // key column names (1+ for composite)
    std::vector<common::ColumnId>  partition_col_idxs;  // resolved indices into the table schema
    std::uint32_t                  num_partitions = 0;
    std::vector<PartitionDef>      defs;                // RANGE: N entries; LIST: N entries; HASH: empty

    bool IsPartitioned() const { return type != PartitionType::NONE; }

    // Route composite partition key to a partition id [0, num_partitions).
    // nkeys must equal partition_cols.size().
    std::uint32_t RouteRow(const common::Value* keys, std::size_t nkeys) const;

    // Single-key convenience wrapper for single-column partitions.
    std::uint32_t RouteRow(const common::Value& key) const { return RouteRow(&key, 1); }

    // Return the subset of partition ids that cannot be excluded by predicates.
    // For composite keys, only the leading key column's predicates are used for pruning.
    std::vector<std::uint32_t> PrunedPartitions(
        const std::vector<ScanPredicate>& predicates) const;
};

} // namespace cppcoldb::engine::storage
