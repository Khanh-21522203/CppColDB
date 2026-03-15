#pragma once
#include "common/types.hpp"

namespace cppcoldb {

// Minimal Transaction stub used by VersionInfo::IsVisible.
// Expanded in Phase 4 with full lifecycle management.
struct Transaction {
    TransactionId tx_id     = INVALID_TRANSACTION;
    timestamp_t   start_time = INVALID_TIMESTAMP;

    static Transaction MakeReadOnly(timestamp_t snapshot_time) {
        Transaction t;
        t.tx_id      = INVALID_TRANSACTION;
        t.start_time = snapshot_time;
        return t;
    }
};

} // namespace cppcoldb
