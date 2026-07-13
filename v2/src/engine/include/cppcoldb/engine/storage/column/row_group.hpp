#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "cppcoldb/common/types/column_id.hpp"
#include "cppcoldb/common/types/data_chunk.hpp"
#include "cppcoldb/common/types/row_group_id.hpp"
#include "cppcoldb/common/types/timestamp.hpp"
#include "cppcoldb/common/types/transaction_id.hpp"
#include "cppcoldb/common/types/type_id.hpp"
#include "cppcoldb/engine/abstractions/storage/i_buffer_manager.hpp"
#include "cppcoldb/engine/storage/column/column_chunk.hpp"
#include "cppcoldb/engine/storage/column/version_info.hpp"
#include "cppcoldb/engine/storage/scan_predicate.hpp"

namespace cppcoldb::engine::transaction { class Transaction; } // owned by another engine domain

namespace cppcoldb::engine::storage {

class RowGroup {
public:
    RowGroup(common::RowGroupId row_group_id, const std::vector<common::TypeId>& col_types,
             IBufferManager& bm);

    // Scan up to STANDARD_VECTOR_SIZE rows starting at row_offset (updated in-place)
    // into chunk for the requested column indices. Applies MVCC visibility filter.
    // Returns the number of rows placed into chunk.
    std::size_t Scan(std::size_t& row_offset, const std::vector<common::ColumnId>& col_ids,
                      common::DataChunk& chunk, const cppcoldb::engine::transaction::Transaction& tx);

    // Zone-map: returns the number of rows to skip at row_offset if any predicate
    // column's segment stats provably exclude all rows in that segment.
    // Returns 0 if no segment can be skipped.
    std::size_t ZoneMapSkipRows(std::size_t row_offset,
                                 const std::vector<ScanPredicate>& predicates) const;

    // Like Scan, but also fills offsets_out with the row-group-relative row offset
    // for each row in the returned chunk (after the MVCC visibility filter).
    std::size_t ScanBatchWithOffsets(std::size_t& row_offset,
                                       const std::vector<common::ColumnId>& col_ids,
                                       common::DataChunk& chunk,
                                       std::vector<std::uint32_t>& offsets_out,
                                       const cppcoldb::engine::transaction::Transaction& tx);

    // Read specific columns for the given row offsets (absolute within this row group).
    // row_offsets must be sorted ascending; MVCC was already applied by the early scan.
    // output is initialized here with late_col_ids.size() columns.
    void ScanLate(const std::vector<std::uint32_t>& row_offsets,
                  const std::vector<common::ColumnId>& late_col_ids,
                  common::DataChunk& output);

    // Append rows from chunk (for the given column indices).
    // If tx_id != INVALID_TRANSACTION, marks rows as uncommitted INSERTs.
    void Append(const common::DataChunk& chunk, const std::vector<common::ColumnId>& col_ids,
                common::TransactionId tx_id = common::INVALID_TRANSACTION);

    // Mark pending uncommitted rows as committed at commit_time.
    void CommitAppend(common::Timestamp commit_time);

    // Remove pending uncommitted rows (rollback).
    void RevertAppend(common::TransactionId tx_id);

    // Flush all ColumnChunks to compressed segment blocks.
    void Flush();

    common::RowGroupId RowGroupId()      const { return row_group_id_; }
    std::size_t         RowCount()       const { return row_count_; }
    VersionInfo&        GetVersionInfo()       { return *version_info_; }
    const VersionInfo&  GetVersionInfo() const { return *version_info_; }

    // For checkpoint serialization / deserialization.
    const std::vector<ColumnChunk>& ColumnChunks() const { return column_chunks_; }
    std::vector<ColumnChunk>&       ColumnChunks()       { return column_chunks_; }
    void SetRowCount(std::size_t n) { row_count_ = n; }

private:
    common::RowGroupId           row_group_id_;
    std::size_t                  row_count_ = 0;
    std::vector<common::TypeId>  col_types_;
    std::vector<ColumnChunk>     column_chunks_;
    std::unique_ptr<VersionInfo> version_info_;
    IBufferManager&              bm_;

    // Tracks the starting row index of the most recent uncommitted Append.
    std::size_t           pending_append_start_  = 0;
    common::TransactionId pending_append_tx_id_   = common::INVALID_TRANSACTION;
};

} // namespace cppcoldb::engine::storage
