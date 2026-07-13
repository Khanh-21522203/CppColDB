#pragma once

#include <cstddef>
#include <mutex>
#include <unordered_map>

#include "cppcoldb/common/types/row_id.hpp"
#include "cppcoldb/common/types/timestamp.hpp"
#include "cppcoldb/common/types/transaction_id.hpp"
#include "cppcoldb/engine/abstractions/transaction/i_version_manager.hpp"

namespace cppcoldb::engine::transaction {

// A single row version's MVCC bookkeeping: who created/deleted it and when.
struct RowVersionInfo {
    common::TransactionId create_tx_id = common::INVALID_TRANSACTION;
    common::Timestamp     create_commit_time = common::INVALID_TIMESTAMP;
    common::TransactionId delete_tx_id = common::INVALID_TRANSACTION;
    common::Timestamp     delete_commit_time = common::INVALID_TIMESTAMP;
};

// Concrete MVCC version manager: tracks per-row create/delete visibility
// bookkeeping in a hash map keyed by RowId, independent of the physical
// storage layout (row groups, blocks, etc. remain storage's concern).
class VersionManager final : public IVersionManager {
public:
    VersionManager() = default;
    ~VersionManager() override = default;

    void RegisterInsert(common::RowId row_id, common::TransactionId tx_id) override;
    void RegisterDelete(common::RowId row_id, common::TransactionId tx_id) override;

    void CommitVersions(common::TransactionId tx_id, common::Timestamp commit_time) override;
    void RollbackVersions(common::TransactionId tx_id) override;

    bool IsVisible(common::RowId row_id, common::Timestamp snapshot_time) const override;

    std::size_t GarbageCollect(common::Timestamp oldest_active_snapshot) override;

private:
    mutable std::mutex mu_;
    std::unordered_map<common::RowId, RowVersionInfo> versions_;
};

} // namespace cppcoldb::engine::transaction
