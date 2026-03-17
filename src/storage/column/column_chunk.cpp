#include "storage/column/column_chunk.hpp"
#include "storage/column/compression.hpp"
#include "storage/buffer_manager.hpp"
#include "common/exception.hpp"

#include <algorithm>
#include <cstring>

namespace cppcoldb {

namespace {

static SegmentStats BuildStats(TypeId type, const DataVector& vec) {
    SegmentStats stats;
    for (size_t i = 0; i < vec.count; ++i) {
        if (vec.IsNull(i)) {
            stats.has_nulls = true;
            continue;
        }
        Value v;
        switch (type) {
            case TypeId::VARCHAR:
                v = Value::Varchar(vec.str_data[i]);
                break;
            case TypeId::FLOAT32:
            case TypeId::FLOAT64:
                v = Value::Float(vec.float_data[i]);
                break;
            default:
                v = Value::Integer(vec.int_data[i]);
                break;
        }
        if (stats.min_val.IsNull() || v < stats.min_val) stats.min_val = v;
        if (stats.max_val.IsNull() || stats.max_val < v) stats.max_val = v;
    }
    return stats;
}

} // namespace

ColumnChunk::ColumnChunk(TypeId type, BufferManager& bm)
    : type_(type), bm_(bm) {
    pending_data_.Reset(type_, 0);
}

size_t ColumnChunk::SegmentRowCount() const {
    size_t total = 0;
    for (const auto& seg : segments_) total += seg.row_count;
    return total;
}

size_t ColumnChunk::RowCount() const {
    return SegmentRowCount() + pending_data_.count;
}

ColumnScanState ColumnChunk::MakeScanState(size_t row_offset) const {
    ColumnScanState state;
    size_t cur = 0;
    for (size_t i = 0; i < segments_.size(); ++i) {
        size_t end = cur + segments_[i].row_count;
        if (row_offset < end) {
            state.segment_idx    = i;
            state.row_in_segment = row_offset - cur;
            return state;
        }
        cur = end;
    }
    // Falls in pending_data_
    state.segment_idx    = segments_.size();
    state.row_in_segment = row_offset - cur;
    return state;
}

size_t ColumnChunk::Scan(ColumnScanState& state, size_t count, DataVector& output) {
    output.Reset(type_, 0);
    size_t rows_read = 0;

    while (rows_read < count) {
        if (state.segment_idx < segments_.size()) {
            const ColumnSegment& seg = segments_[state.segment_idx];
            BufferHandle handle = bm_.Pin(seg.block_id);

            DataVector seg_vec;
            Decompress(handle.Data(), bm_.BlockSize(), type_, seg_vec);

            size_t available = seg.row_count - state.row_in_segment;
            size_t to_copy   = std::min(count - rows_read, available);

            for (size_t j = 0; j < to_copy; ++j) {
                DataVectorAppend(output, seg_vec, state.row_in_segment + j);
            }

            rows_read            += to_copy;
            state.row_in_segment += to_copy;

            if (state.row_in_segment >= seg.row_count) {
                ++state.segment_idx;
                state.row_in_segment = 0;
            }

        } else if (state.segment_idx == segments_.size()) {
            size_t available = pending_data_.count - state.row_in_segment;
            if (available == 0) break;

            size_t to_copy = std::min(count - rows_read, available);

            for (size_t j = 0; j < to_copy; ++j) {
                DataVectorAppend(output, pending_data_, state.row_in_segment + j);
            }

            rows_read            += to_copy;
            state.row_in_segment += to_copy;

            if (state.row_in_segment >= pending_data_.count) {
                ++state.segment_idx;
                state.row_in_segment = 0;
            }

        } else {
            break; // past end
        }
    }

    return rows_read;
}

void ColumnChunk::Flush() {
    if (pending_data_.count == 0) return;

    CompressionChoice choice = SelectCompression(pending_data_);
    BufferHandle handle = bm_.AllocateBlock();

    Compress(choice, pending_data_, handle.Data(), bm_.BlockSize());
    handle.MarkDirty();

    ColumnSegment seg;
    seg.block_id    = handle.Id();
    seg.compression = choice.type;
    seg.column_type = type_;
    seg.row_count   = static_cast<uint32_t>(pending_data_.count);
    seg.row_offset  = static_cast<uint32_t>(pending_row_offset_);
    seg.stats       = combined_stats_;
    segments_.push_back(seg);

    pending_row_offset_ += pending_data_.count;
    pending_data_.Reset(type_, 0);
}

void ColumnChunk::AppendFromVector(const DataVector& vec, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (pending_data_.count >= STANDARD_VECTOR_SIZE) {
            Flush();
        }
        DataVectorAppend(pending_data_, vec, i);
    }
    UpdateCombinedStats(vec, count);
}

void ColumnChunk::TruncatePending(size_t new_count) {
    if (new_count >= pending_data_.count) return;
    for (size_t i = new_count; i < pending_data_.count; ++i) {
        pending_data_.validity.reset(i);
    }
    pending_data_.count = new_count;
}

void ColumnChunk::WriteRow(uint32_t row_offset, const DataVector& src, size_t src_idx) {
    auto write_value = [&](DataVector& target, size_t idx) {
        if (!src.IsNull(src_idx)) {
            target.validity.set(idx);
            switch (type_) {
                case TypeId::BOOLEAN:
                case TypeId::INT8:
                case TypeId::INT16:
                case TypeId::INT32:
                case TypeId::INT64:
                    target.int_data[idx] = src.int_data[src_idx];
                    break;
                case TypeId::FLOAT32:
                case TypeId::FLOAT64:
                    target.float_data[idx] = src.float_data[src_idx];
                    break;
                case TypeId::VARCHAR:
                    target.str_data[idx] = src.str_data[src_idx];
                    break;
                default:
                    break;
            }
        } else {
            target.validity.reset(idx);
        }
    };

    size_t cur = 0;
    for (size_t seg_idx = 0; seg_idx < segments_.size(); ++seg_idx) {
        auto& seg = segments_[seg_idx];
        size_t end = cur + seg.row_count;
        if (row_offset < end) {
            BufferHandle handle = bm_.Pin(seg.block_id);
            DataVector seg_vec;
            Decompress(handle.Data(), bm_.BlockSize(), type_, seg_vec);
            size_t idx = row_offset - cur;
            write_value(seg_vec, idx);

            CompressionChoice choice = SelectCompression(seg_vec);
            Compress(choice, seg_vec, handle.Data(), bm_.BlockSize());
            handle.MarkDirty();
            seg.compression = choice.type;
            seg.stats = BuildStats(type_, seg_vec);
            return;
        }
        cur = end;
    }

    // Falls in pending_data_.
    size_t pending_idx = row_offset - cur;
    if (pending_idx >= pending_data_.count) {
        throw RuntimeError("ColumnChunk::WriteRow: row_offset out of range");
    }
    write_value(pending_data_, pending_idx);
}

void ColumnChunk::UpdateCombinedStats(const DataVector& vec, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (vec.IsNull(i)) {
            combined_stats_.has_nulls = true;
            continue;
        }
        Value v;
        switch (type_) {
            case TypeId::VARCHAR:
                v = Value::Varchar(vec.str_data[i]);
                break;
            case TypeId::FLOAT32:
            case TypeId::FLOAT64:
                v = Value::Float(vec.float_data[i]);
                break;
            default:
                v = Value::Integer(vec.int_data[i]);
                break;
        }
        if (combined_stats_.min_val.IsNull() || v < combined_stats_.min_val)
            combined_stats_.min_val = v;
        if (combined_stats_.max_val.IsNull() || combined_stats_.max_val < v)
            combined_stats_.max_val = v;
    }
}

} // namespace cppcoldb
