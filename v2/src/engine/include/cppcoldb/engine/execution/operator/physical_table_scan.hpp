#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cppcoldb/common/types/column_id.hpp"
#include "cppcoldb/engine/execution/physical_operator.hpp"
#include "cppcoldb/engine/storage/scan_predicate.hpp"

namespace cppcoldb::engine::execution {

// Scan cursor over one table: which RowGroup/offset within it is next, and
// (for partitioned tables) which partition is currently being iterated.
struct TableScanState : public OperatorState {
    std::size_t row_group_idx = 0;
    std::size_t row_offset_in_group = 0;
    std::size_t partition_idx = 0;
};

// SOURCE: sequentially scans a table's committed + this-transaction's
// uncommitted RowGroups, applying pushed-down zone-map predicates and
// (for partitioned tables) partition pruning before rows are ever
// materialized into a DataChunk.
class PhysicalTableScan : public PhysicalOperator {
public:
    PhysicalTableScan() { role = OperatorRole::SOURCE; }

    std::unique_ptr<OperatorState> CreateScanState() const override;
    void InitScan(OperatorState& state, ::cppcoldb::ClientContext& ctx) override;
    OperatorResultType GetData(OperatorState& state, common::DataChunk& output,
                                ::cppcoldb::ClientContext& ctx) override;

    std::string                            schema_name;
    std::string                            table_name;
    std::vector<common::ColumnId>          column_ids;
    std::vector<storage::ScanPredicate>    scan_predicates;
    std::vector<std::uint32_t>             active_partition_ids;
};

} // namespace cppcoldb::engine::execution
