#include "cppcoldb/engine/storage/partition_info.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::storage {

std::uint32_t PartitionInfo::RouteRow(const common::Value* /*keys*/, std::size_t /*nkeys*/) const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

std::vector<std::uint32_t> PartitionInfo::PrunedPartitions(
    const std::vector<ScanPredicate>& /*predicates*/) const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::storage
