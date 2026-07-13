#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "cppcoldb/common/types/isolation_level.hpp"
#include "cppcoldb/common/types/timestamp.hpp"
#include "cppcoldb/common/types/transaction_id.hpp"
#include "cppcoldb/engine/abstractions/catalog/i_catalog.hpp"
#include "cppcoldb/engine/abstractions/transaction/i_transaction_manager.hpp"
#include "cppcoldb/engine/abstractions/transaction/i_version_manager.hpp"
#include "cppcoldb/engine/transaction/transaction.hpp"

namespace cppcoldb::engine::transaction {

// Concrete transaction manager: assigns transaction/commit timestamps, tracks
// active transactions, and drives the catalog's and version manager's
// commit/rollback lifecycle for each transaction's changes.
class TransactionManager final : public ITransactionManager {
public:
    TransactionManager(catalog::ICatalog& catalog, IVersionManager& version_manager);
    ~TransactionManager() override = default;

    std::shared_ptr<ITransaction> BeginTransaction(
        bool auto_commit = true,
        common::IsolationLevel isolation = common::IsolationLevel::SNAPSHOT) override;
    void Commit(std::shared_ptr<ITransaction> tx) override;
    void Rollback(std::shared_ptr<ITransaction> tx) override;

    common::Timestamp CurrentCommitTime() const override;
    std::size_t        ActiveTransactionCount() const override;

private:
    void ApplyUndoBuffer(Transaction& tx);
    void UndoBufferReverse(Transaction& tx);
    void GarbageCollect();

    catalog::ICatalog& catalog_;
    IVersionManager&    version_manager_;

    std::atomic<std::uint64_t> next_tx_id_{1};
    std::atomic<std::int64_t>  commit_counter_{0};

    mutable std::mutex mu_;
    std::unordered_map<common::TransactionId, std::shared_ptr<Transaction>> active_transactions_;
};

} // namespace cppcoldb::engine::transaction
