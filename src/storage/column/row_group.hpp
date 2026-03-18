#pragma once
#include <vector>
#include <memory>
#include "storage/column/column_chunk.hpp"
#include "storage/column/version_info.hpp"
#include "storage/scan_predicate.hpp"

namespace cppcoldb {

class Transaction;
class BufferManager;

class RowGroup {
public:
    RowGroup(uint32_t row_group_id, const std::vector<TypeId>& col_types,
             BufferManager& bm);

    // Scan up to STANDARD_VECTOR_SIZE rows starting at row_offset (updated in-place)
    // into chunk for the requested column indices. Applies MVCC visibility filter.
    // Returns the number of rows placed into chunk.
    size_t Scan(size_t& row_offset, const std::vector<size_t>& col_ids,
                DataChunk& chunk, const Transaction& tx);

    // Zone-map: returns the number of rows to skip at row_offset if any predicate
    // column's segment stats provably exclude all rows in that segment.
    // Returns 0 if no segment can be skipped.
    size_t ZoneMapSkipRows(size_t row_offset,
                           const std::vector<ScanPredicate>& predicates) const;

    // Like Scan, but also fills offsets_out with the row_group-relative row offset
    // for each row in the returned chunk (after MVCC visibility filter).
    size_t ScanBatchWithOffsets(size_t& row_offset, const std::vector<size_t>& col_ids,
                                 DataChunk& chunk, std::vector<uint32_t>& offsets_out,
                                 const Transaction& tx);

    // Read specific columns for the given row offsets (absolute within this row group).
    // row_offsets must be sorted ascending; MVCC was already applied by the early scan.
    // output is initialized here with late_col_ids.size() columns.
    void ScanLate(const std::vector<uint32_t>& row_offsets,
                  const std::vector<size_t>& late_col_ids,
                  DataChunk& output);

    // Append rows from chunk (for the given column indices).
    // If tx_id != INVALID_TRANSACTION, marks rows as uncommitted INSERTs.
    void Append(const DataChunk& chunk, const std::vector<size_t>& col_ids,
                TransactionId tx_id = INVALID_TRANSACTION);

    // Mark pending uncommitted rows as committed at commit_time.
    void CommitAppend(timestamp_t commit_time);

    // Remove pending uncommitted rows (rollback).
    void RevertAppend(TransactionId tx_id);

    // Flush all ColumnChunks to compressed segment blocks.
    void Flush();

    uint32_t     RowGroupId()   const { return row_group_id_; }
    size_t       RowCount()    const { return row_count_; }
    VersionInfo& GetVersionInfo()    { return *version_info_; }
    const VersionInfo& GetVersionInfo() const { return *version_info_; }

    // For checkpoint serialization / deserialization.
    const std::vector<ColumnChunk>& ColumnChunks() const { return column_chunks_; }
    std::vector<ColumnChunk>&       ColumnChunks()       { return column_chunks_; }
    void SetRowCount(size_t n)                           { row_count_ = n; }

private:
    uint32_t                     row_group_id_;
    size_t                       row_count_ = 0;
    std::vector<TypeId>          col_types_;
    std::vector<ColumnChunk>     column_chunks_;
    std::unique_ptr<VersionInfo> version_info_;
    BufferManager&               bm_;

    // Tracks the starting row index of the most recent uncommitted Append.
    size_t      pending_append_start_ = 0;
    TransactionId pending_append_tx_id_ = INVALID_TRANSACTION;
};

} // namespace cppcoldb
