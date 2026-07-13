#pragma once
#include <cstdint>
#include <unordered_map>

#include "cppcoldb/common/types/timestamp.hpp"
#include "cppcoldb/common/types/transaction_id.hpp"

namespace cppcoldb::engine::transaction { class Transaction; } // owned by another engine domain

namespace cppcoldb::engine::storage {

enum class VersionMarkerType : std::uint8_t {
    DELETE,
    INSERT,
    UPDATE,
};

struct VersionMarker {
    VersionMarkerType      type;
    common::TransactionId  tx_id;
    common::Timestamp      commit_time = common::INVALID_TIMESTAMP; // INVALID_TIMESTAMP = uncommitted
};

// Per-RowGroup MVCC version markers.
// Maps row_offset -> most recent VersionMarker for that row.
class VersionInfo {
public:
    VersionInfo() = default;

    // Delete markers
    void MarkDeleted(std::uint32_t row_offset, common::TransactionId tx_id);
    void CommitDelete(std::uint32_t row_offset, common::Timestamp commit_time);
    void ClearDeleteMarker(std::uint32_t row_offset);

    // Insert markers (used for uncommitted appends)
    void MarkInserted(std::uint32_t row_offset, common::TransactionId tx_id);
    void CommitInsert(std::uint32_t row_offset, common::Timestamp commit_time);
    void RevertInsert(std::uint32_t row_offset);

    // Update markers
    void MarkUpdated(std::uint32_t row_offset, common::TransactionId tx_id);
    void CommitUpdate(std::uint32_t row_offset, common::Timestamp commit_time);
    void RevertUpdate(std::uint32_t row_offset);

    // Returns true if row_offset should be included for the given transaction.
    bool IsVisible(std::uint32_t row_offset, const cppcoldb::engine::transaction::Transaction& tx) const;

    bool HasAnyMarkers() const { return !markers_.empty(); }
    bool HasUncommittedMarkers() const;

    // O(1) fast path: true when every marker is a committed INSERT with
    // commit_time < tx_start, meaning every row is visible and the per-row
    // IsVisible loop in RowGroup::Scan can be skipped entirely.
    bool AllInsertedVisibleTo(common::Timestamp tx_start) const {
        return uncommitted_count_ == 0
            && delete_update_count_ == 0
            && max_insert_commit_time_ < tx_start;
    }

private:
    std::unordered_map<std::uint32_t, VersionMarker> markers_;
    int               uncommitted_count_      = 0; // markers with INVALID_TIMESTAMP
    int               delete_update_count_    = 0; // DELETE + UPDATE markers
    common::Timestamp max_insert_commit_time_ = common::Timestamp{0}; // max commit_time of any INSERT marker
};

} // namespace cppcoldb::engine::storage
