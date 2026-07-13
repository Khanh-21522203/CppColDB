#pragma once

#include <string>
#include <unordered_map>

#include "cppcoldb/common/types/data_chunk.hpp"
#include "cppcoldb/common/types/isolation_level.hpp"
#include "cppcoldb/common/types/timestamp.hpp"
#include "cppcoldb/common/types/transaction_id.hpp"
#include "cppcoldb/engine/abstractions/transaction/i_transaction.hpp"
#include "cppcoldb/engine/transaction/undo_buffer.hpp"

namespace cppcoldb::engine::transaction {

// Concrete transaction: identity, MVCC snapshot bounds, its undo log, and the
// uncommitted local row storage staged for merge into tables on commit.
class Transaction final : public ITransaction {
public:
    Transaction() = default;
    Transaction(common::TransactionId tx_id, common::Timestamp start_time, bool auto_commit,
                common::IsolationLevel isolation = common::IsolationLevel::SNAPSHOT)
        : tx_id_(tx_id), start_time_(start_time), auto_commit_(auto_commit), isolation_(isolation) {}
    ~Transaction() override = default;

    common::TransactionId  Id() const override { return tx_id_; }
    common::Timestamp      StartTime() const override { return start_time_; }
    common::Timestamp      CommitTime() const override { return commit_time_; }
    common::IsolationLevel Isolation() const override { return isolation_; }
    bool                   IsAutoCommit() const override { return auto_commit_; }
    bool                   IsInvalid() const override { return is_invalid_; }

    void SetCommitTime(common::Timestamp commit_time) { commit_time_ = commit_time; }
    void MarkInvalid() { is_invalid_ = true; }

    UndoBuffer&       Undo() { return undo_buffer_; }
    const UndoBuffer& Undo() const { return undo_buffer_; }

    // Uncommitted rows staged per "schema.table" key, merged on commit.
    std::unordered_map<std::string, common::DataChunk>&       LocalStorage() { return local_storage_; }
    const std::unordered_map<std::string, common::DataChunk>& LocalStorage() const {
        return local_storage_;
    }

private:
    common::TransactionId  tx_id_ = common::INVALID_TRANSACTION;
    common::Timestamp      start_time_ = common::INVALID_TIMESTAMP;
    common::Timestamp      commit_time_ = common::INVALID_TIMESTAMP;
    bool                   auto_commit_ = false;
    bool                   is_invalid_ = false;
    common::IsolationLevel isolation_ = common::IsolationLevel::SNAPSHOT;

    UndoBuffer undo_buffer_;
    std::unordered_map<std::string, common::DataChunk> local_storage_;
};

} // namespace cppcoldb::engine::transaction
