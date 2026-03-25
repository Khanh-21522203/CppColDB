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
// For RANGE: upper_bound is the exclusive upper bound (IsNull = +inf for last partition).
// For LIST:  list_values are the discrete values routed to this partition.
// For HASH:  defs is empty (routing is hash(key) % num_partitions).
struct PartitionDef {
    Value              upper_bound;  // RANGE only
    std::vector<Value> list_values;  // LIST only
};

struct PartitionInfo {
    PartitionType type              = PartitionType::NONE;
    std::string   partition_col;
    int           partition_col_idx = -1;  // resolved index into TableCatalogEntry::columns
    uint32_t      num_partitions    = 0;
    std::vector<PartitionDef> defs; // RANGE: N entries; LIST: N entries; HASH: empty

    bool IsPartitioned() const { return type != PartitionType::NONE; }

    // Route a partition key value to a partition id [0, num_partitions).
    uint32_t RouteRow(const Value& key) const;

    // Return the subset of partition ids that cannot be excluded by predicates.
    // predicates must use table-column indices (not chunk positions).
    std::vector<uint32_t> PrunedPartitions(
        const std::vector<ScanPredicate>& predicates) const;
};

} // namespace cppcoldb
