#include "storage/column/row_group.hpp"
#include "transaction/transaction.hpp"
#include "common/types.hpp"

#include <algorithm>
#include <cassert>

namespace cppcoldb {

RowGroup::RowGroup(uint32_t row_group_id, const std::vector<TypeId>& col_types,
                   BufferManager& bm)
    : row_group_id_(row_group_id), col_types_(col_types), bm_(bm),
      version_info_(std::make_unique<VersionInfo>()) {
    column_chunks_.reserve(col_types.size());
    for (TypeId t : col_types) {
        column_chunks_.emplace_back(t, bm_);
    }
}

size_t RowGroup::Scan(size_t& row_offset, const std::vector<size_t>& col_ids,
                      DataChunk& chunk, const Transaction& tx) {
    if (row_offset >= row_count_) return 0;

    size_t rows_to_read = std::min(STANDARD_VECTOR_SIZE, row_count_ - row_offset);

    // Collect types for the requested columns.
    std::vector<TypeId> out_types;
    out_types.reserve(col_ids.size());
    for (size_t c : col_ids) out_types.push_back(col_types_[c]);

    chunk.Initialize(out_types);

    // Read each requested column.
    for (size_t ci = 0; ci < col_ids.size(); ++ci) {
        ColumnScanState state = column_chunks_[col_ids[ci]].MakeScanState(row_offset);
        column_chunks_[col_ids[ci]].Scan(state, rows_to_read, chunk.columns[ci]);
    }
    chunk.count = rows_to_read;

    // Apply MVCC visibility filter.
    if (version_info_->HasAnyMarkers()) {
        std::vector<uint32_t> selection;
        selection.reserve(rows_to_read);
        for (size_t i = 0; i < rows_to_read; ++i) {
            if (version_info_->IsVisible(static_cast<uint32_t>(row_offset + i), tx)) {
                selection.push_back(static_cast<uint32_t>(i));
            }
        }
        if (selection.size() < rows_to_read) {
            DataChunk filtered;
            filtered.Initialize(out_types);
            DataChunkSlice(filtered, chunk, selection);
            chunk = std::move(filtered);
        }
    }

    row_offset += rows_to_read;
    return chunk.count;
}

size_t RowGroup::ScanBatchWithOffsets(size_t& row_offset,
                                       const std::vector<size_t>& col_ids,
                                       DataChunk& chunk,
                                       std::vector<uint32_t>& offsets_out,
                                       const Transaction& tx) {
    if (row_offset >= row_count_) return 0;

    size_t rows_to_read = std::min(STANDARD_VECTOR_SIZE, row_count_ - row_offset);

    std::vector<TypeId> out_types;
    out_types.reserve(col_ids.size());
    for (size_t c : col_ids) out_types.push_back(col_types_[c]);

    chunk.Initialize(out_types);

    for (size_t ci = 0; ci < col_ids.size(); ++ci) {
        ColumnScanState state = column_chunks_[col_ids[ci]].MakeScanState(row_offset);
        column_chunks_[col_ids[ci]].Scan(state, rows_to_read, chunk.columns[ci]);
    }
    chunk.count = rows_to_read;

    // Apply MVCC + collect per-row offsets.
    std::vector<uint32_t> selection;
    selection.reserve(rows_to_read);
    offsets_out.clear();
    for (size_t i = 0; i < rows_to_read; ++i) {
        if (version_info_->IsVisible(static_cast<uint32_t>(row_offset + i), tx)) {
            selection.push_back(static_cast<uint32_t>(i));
            offsets_out.push_back(static_cast<uint32_t>(row_offset + i));
        }
    }

    if (selection.size() < rows_to_read) {
        DataChunk filtered;
        filtered.Initialize(out_types);
        DataChunkSlice(filtered, chunk, selection);
        chunk = std::move(filtered);
    }

    row_offset += rows_to_read;
    return chunk.count;
}

void RowGroup::Append(const DataChunk& chunk, const std::vector<size_t>& col_ids,
                      TransactionId tx_id) {
    pending_append_start_ = row_count_;
    pending_append_tx_id_ = tx_id;

    for (size_t ci = 0; ci < col_ids.size(); ++ci) {
        column_chunks_[col_ids[ci]].AppendFromVector(chunk.columns[ci], chunk.count);
    }

    if (tx_id != INVALID_TRANSACTION) {
        for (size_t i = 0; i < chunk.count; ++i) {
            version_info_->MarkInserted(
                static_cast<uint32_t>(row_count_ + i), tx_id);
        }
    }

    row_count_ += chunk.count;
}

void RowGroup::CommitAppend(timestamp_t commit_time) {
    for (size_t i = pending_append_start_; i < row_count_; ++i) {
        version_info_->CommitInsert(static_cast<uint32_t>(i), commit_time);
    }
}

void RowGroup::RevertAppend(TransactionId tx_id) {
    size_t revert_count = row_count_ - pending_append_start_;

    // Remove INSERT markers for the pending rows.
    for (size_t i = pending_append_start_; i < row_count_; ++i) {
        version_info_->RevertInsert(static_cast<uint32_t>(i));
    }

    // Truncate each ColumnChunk's pending data.
    for (auto& cc : column_chunks_) {
        size_t seg_rows = cc.SegmentRowCount();
        if (pending_append_start_ >= seg_rows) {
            cc.TruncatePending(pending_append_start_ - seg_rows);
        } else {
            // Append touched flushed segments — not supported in Phase 3.
            cc.TruncatePending(0);
        }
    }

    row_count_ = pending_append_start_;
    (void)tx_id;
    (void)revert_count;
}

void RowGroup::Flush() {
    for (auto& cc : column_chunks_) {
        cc.Flush();
    }
}

} // namespace cppcoldb
