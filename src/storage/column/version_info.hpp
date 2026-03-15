#pragma once
#include <unordered_map>
#include "common/types.hpp"

namespace cppcoldb {

struct Transaction; // defined in transaction/transaction.hpp

enum class VersionMarkerType : uint8_t {
    DELETE,
    INSERT,
    UPDATE,
};

struct VersionMarker {
    VersionMarkerType type;
    TransactionId     tx_id;
    timestamp_t       commit_time; // INVALID_TIMESTAMP = uncommitted
};

// Per-RowGroup MVCC version markers.
// Maps row_offset → most recent VersionMarker for that row.
class VersionInfo {
public:
    // Delete markers
    void MarkDeleted(uint32_t row_offset, TransactionId tx_id);
    void CommitDelete(uint32_t row_offset, timestamp_t commit_time);
    void ClearDeleteMarker(uint32_t row_offset);

    // Insert markers (used for uncommitted appends)
    void MarkInserted(uint32_t row_offset, TransactionId tx_id);
    void CommitInsert(uint32_t row_offset, timestamp_t commit_time);
    void RevertInsert(uint32_t row_offset);

    // Update markers
    void MarkUpdated(uint32_t row_offset, TransactionId tx_id);
    void CommitUpdate(uint32_t row_offset, timestamp_t commit_time);
    void RevertUpdate(uint32_t row_offset);

    // Returns true if row_offset should be included for the given transaction.
    bool IsVisible(uint32_t row_offset, const Transaction& tx) const;

    bool HasAnyMarkers() const { return !markers_.empty(); }

private:
    std::unordered_map<uint32_t, VersionMarker> markers_;
};

} // namespace cppcoldb
