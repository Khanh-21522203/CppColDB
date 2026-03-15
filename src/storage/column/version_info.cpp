#include "storage/column/version_info.hpp"
#include "transaction/transaction.hpp"

namespace cppcoldb {

void VersionInfo::MarkDeleted(uint32_t row_offset, TransactionId tx_id) {
    markers_[row_offset] = {VersionMarkerType::DELETE, tx_id, INVALID_TIMESTAMP};
}

void VersionInfo::CommitDelete(uint32_t row_offset, timestamp_t commit_time) {
    auto it = markers_.find(row_offset);
    if (it != markers_.end()) {
        it->second.commit_time = commit_time;
    }
}

void VersionInfo::ClearDeleteMarker(uint32_t row_offset) {
    markers_.erase(row_offset);
}

void VersionInfo::MarkInserted(uint32_t row_offset, TransactionId tx_id) {
    markers_[row_offset] = {VersionMarkerType::INSERT, tx_id, INVALID_TIMESTAMP};
}

void VersionInfo::CommitInsert(uint32_t row_offset, timestamp_t commit_time) {
    auto it = markers_.find(row_offset);
    if (it != markers_.end()) {
        it->second.commit_time = commit_time;
    }
}

void VersionInfo::RevertInsert(uint32_t row_offset) {
    markers_.erase(row_offset);
}

void VersionInfo::MarkUpdated(uint32_t row_offset, TransactionId tx_id) {
    markers_[row_offset] = {VersionMarkerType::UPDATE, tx_id, INVALID_TIMESTAMP};
}

void VersionInfo::CommitUpdate(uint32_t row_offset, timestamp_t commit_time) {
    auto it = markers_.find(row_offset);
    if (it != markers_.end()) {
        it->second.commit_time = commit_time;
    }
}

void VersionInfo::RevertUpdate(uint32_t row_offset) {
    markers_.erase(row_offset);
}

bool VersionInfo::IsVisible(uint32_t row_offset, const Transaction& tx) const {
    auto it = markers_.find(row_offset);
    if (it == markers_.end()) return true; // unmodified row — always visible

    const VersionMarker& m = it->second;

    switch (m.type) {
        case VersionMarkerType::DELETE:
            if (m.tx_id == tx.tx_id)
                return false; // own delete — hidden from self
            if (m.commit_time == INVALID_TIMESTAMP)
                return true;  // uncommitted delete by another — still visible
            if (m.commit_time < tx.start_time)
                return false; // delete committed before T started — not visible
            return true;      // delete committed after T started — still visible

        case VersionMarkerType::INSERT:
            if (m.tx_id == tx.tx_id)
                return true;  // own insert — visible
            if (m.commit_time == INVALID_TIMESTAMP)
                return false; // uncommitted insert by another — not visible
            if (m.commit_time < tx.start_time)
                return true;  // committed before T started — visible
            return false;     // committed after T started — not visible

        case VersionMarkerType::UPDATE:
            return true; // row is visible; updated value resolved via UndoBuffer in Phase 4
    }

    return true;
}

} // namespace cppcoldb
