#include "cppcoldb/engine/storage/column/row_group.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::storage {

RowGroup::RowGroup(common::RowGroupId row_group_id, const std::vector<common::TypeId>& col_types,
                    IBufferManager& bm)
    : row_group_id_(row_group_id), col_types_(col_types), bm_(bm) {}

std::size_t RowGroup::Scan(std::size_t& /*row_offset*/, const std::vector<common::ColumnId>& /*col_ids*/,
                            common::DataChunk& /*chunk*/,
                            const cppcoldb::engine::transaction::Transaction& /*tx*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

std::size_t RowGroup::ZoneMapSkipRows(std::size_t /*row_offset*/,
                                       const std::vector<ScanPredicate>& /*predicates*/) const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

std::size_t RowGroup::ScanBatchWithOffsets(std::size_t& /*row_offset*/,
                                            const std::vector<common::ColumnId>& /*col_ids*/,
                                            common::DataChunk& /*chunk*/,
                                            std::vector<std::uint32_t>& /*offsets_out*/,
                                            const cppcoldb::engine::transaction::Transaction& /*tx*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void RowGroup::ScanLate(const std::vector<std::uint32_t>& /*row_offsets*/,
                         const std::vector<common::ColumnId>& /*late_col_ids*/,
                         common::DataChunk& /*output*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void RowGroup::Append(const common::DataChunk& /*chunk*/, const std::vector<common::ColumnId>& /*col_ids*/,
                       common::TransactionId /*tx_id*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void RowGroup::CommitAppend(common::Timestamp /*commit_time*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void RowGroup::RevertAppend(common::TransactionId /*tx_id*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void RowGroup::Flush() { CPPCOLDB_NOT_IMPLEMENTED(); }

} // namespace cppcoldb::engine::storage
