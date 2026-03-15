#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include "common/types.hpp"
#include "transaction/undo_buffer.hpp"

namespace cppcoldb {

class WAL;

class Transaction {
public:
    Transaction() = default;
    Transaction(TransactionId tx_id_, timestamp_t start_time_, bool auto_commit_)
        : tx_id(tx_id_), start_time(start_time_), auto_commit(auto_commit_) {}

    // Write undo buffer changes to WAL (stub — full impl in Phase 9).
    void WriteToWAL(WAL& wal) const;

    // Backward-compat factory used by Phase 3 tests.
    static Transaction MakeReadOnly(timestamp_t snapshot_time) {
        Transaction t;
        t.tx_id      = INVALID_TRANSACTION;
        t.start_time = snapshot_time;
        return t;
    }

    TransactionId tx_id      = INVALID_TRANSACTION;
    timestamp_t   start_time = INVALID_TIMESTAMP;
    timestamp_t   commit_time = INVALID_TIMESTAMP;
    bool          auto_commit = false;
    bool          is_invalid  = false;

    UndoBuffer undo_buffer;

    // Private rows waiting to be merged into RowGroup on commit.
    // key = "schema.table"
    std::unordered_map<std::string, DataChunk> local_storage;
};

} // namespace cppcoldb
