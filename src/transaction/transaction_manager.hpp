#pragma once
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <memory>
#include "transaction/transaction.hpp"

namespace cppcoldb {

class Catalog;
class WAL;

class TransactionManager {
public:
    explicit TransactionManager(Catalog& catalog, WAL* wal = nullptr);

    std::shared_ptr<Transaction> BeginTransaction(bool auto_commit = true);
    void Commit  (std::shared_ptr<Transaction> tx);
    void Rollback(std::shared_ptr<Transaction> tx);

    timestamp_t CurrentCommitTime() const {
        return commit_counter_.load(std::memory_order_acquire);
    }

    size_t ActiveTransactionCount() const;

private:
    void ApplyUndoBuffer (Transaction& tx);
    void UndoBufferReverse(Transaction& tx);
    void GarbageCollect();

    Catalog& catalog_;
    WAL*     wal_;

    std::atomic<TransactionId> next_tx_id_     {1};
    // commit_counter_ starts at 0; start_time = load()+1; commit_time = fetch_add(1)+1.
    std::atomic<timestamp_t>   commit_counter_ {0};

    mutable std::mutex mu_;
    std::unordered_map<TransactionId, std::shared_ptr<Transaction>> active_transactions_;

    std::mutex wal_write_mu_;
};

} // namespace cppcoldb
