#include "transaction/transaction_manager.hpp"
#include "catalog/catalog.hpp"
#include "catalog/catalog_entry.hpp"
#include "storage/column/row_group.hpp"
#include "common/exception.hpp"

namespace cppcoldb {

TransactionManager::TransactionManager(Catalog& catalog, WAL* wal)
    : catalog_(catalog), wal_(wal) {}

std::shared_ptr<Transaction> TransactionManager::BeginTransaction(bool auto_commit) {
    TransactionId id    = next_tx_id_.fetch_add(1, std::memory_order_relaxed);
    timestamp_t   start = commit_counter_.load(std::memory_order_acquire) + 1;
    auto tx = std::make_shared<Transaction>(id, start, auto_commit);
    {
        std::lock_guard<std::mutex> lk(mu_);
        active_transactions_[id] = tx;
    }
    return tx;
}

void TransactionManager::Commit(std::shared_ptr<Transaction> tx) {
    if (!tx->undo_buffer.IsEmpty() && wal_) {
        std::lock_guard<std::mutex> wlk(wal_write_mu_);
        tx->WriteToWAL(*wal_);
    }

    tx->commit_time =
        commit_counter_.fetch_add(1, std::memory_order_acq_rel) + 1;

    ApplyUndoBuffer(*tx);

    {
        std::lock_guard<std::mutex> lk(mu_);
        active_transactions_.erase(tx->tx_id);
    }
    GarbageCollect();
}

void TransactionManager::Rollback(std::shared_ptr<Transaction> tx) {
    UndoBufferReverse(*tx);
    tx->local_storage.clear();
    {
        std::lock_guard<std::mutex> lk(mu_);
        active_transactions_.erase(tx->tx_id);
    }
    GarbageCollect();
}

void TransactionManager::ApplyUndoBuffer(Transaction& tx) {
    tx.undo_buffer.ForEachForward([&](const UndoEntry& entry) {
        if (const auto* e = std::get_if<CatalogUndoEntry>(&entry)) {
            catalog_.CommitEntry(e->schema, e->table, tx.tx_id, tx.commit_time);

        } else if (const auto* e = std::get_if<InsertUndoEntry>(&entry)) {
            auto* table = catalog_.GetTable(e->schema, e->table, tx);
            if (table && e->row_group_id < table->row_groups.size()) {
                table->row_groups[e->row_group_id]->CommitAppend(tx.commit_time);
            }

        } else if (const auto* e = std::get_if<DeleteUndoEntry>(&entry)) {
            auto* table = catalog_.GetTable(e->schema, e->table, tx);
            if (table) {
                for (row_t row_id : e->row_ids) {
                    uint32_t rg_idx = RowIdGroup(row_id);
                    uint32_t offset = RowIdOffset(row_id);
                    if (rg_idx < table->row_groups.size()) {
                        table->row_groups[rg_idx]->GetVersionInfo()
                            .CommitDelete(offset, tx.commit_time);
                    }
                }
            }

        } else if (const auto* e = std::get_if<UpdateUndoEntry>(&entry)) {
            auto* table = catalog_.GetTable(e->schema, e->table, tx);
            if (table) {
                for (row_t row_id : e->row_ids) {
                    uint32_t rg_idx = RowIdGroup(row_id);
                    uint32_t offset = RowIdOffset(row_id);
                    if (rg_idx < table->row_groups.size()) {
                        table->row_groups[rg_idx]->GetVersionInfo()
                            .CommitUpdate(offset, tx.commit_time);
                    }
                }
            }
        }
    });
}

void TransactionManager::UndoBufferReverse(Transaction& tx) {
    tx.undo_buffer.ForEachReverse([&](const UndoEntry& entry) {
        if (const auto* e = std::get_if<CatalogUndoEntry>(&entry)) {
            if (e->was_create) {
                catalog_.RollbackCreate(e->schema, e->table, tx.tx_id);
            } else {
                catalog_.RollbackDrop(e->schema, e->table, tx.tx_id);
            }

        } else if (const auto* e = std::get_if<InsertUndoEntry>(&entry)) {
            auto* table = catalog_.GetTable(e->schema, e->table, tx);
            if (table && e->row_group_id < table->row_groups.size()) {
                table->row_groups[e->row_group_id]->RevertAppend(tx.tx_id);
            }

        } else if (const auto* e = std::get_if<DeleteUndoEntry>(&entry)) {
            auto* table = catalog_.GetTable(e->schema, e->table, tx);
            if (table) {
                for (row_t row_id : e->row_ids) {
                    uint32_t rg_idx = RowIdGroup(row_id);
                    uint32_t offset = RowIdOffset(row_id);
                    if (rg_idx < table->row_groups.size()) {
                        table->row_groups[rg_idx]->GetVersionInfo()
                            .ClearDeleteMarker(offset);
                    }
                }
            }

        } else if (const auto* e = std::get_if<UpdateUndoEntry>(&entry)) {
            auto* table = catalog_.GetTable(e->schema, e->table, tx);
            if (table) {
                for (row_t row_id : e->row_ids) {
                    uint32_t rg_idx = RowIdGroup(row_id);
                    uint32_t offset = RowIdOffset(row_id);
                    if (rg_idx < table->row_groups.size()) {
                        table->row_groups[rg_idx]->GetVersionInfo()
                            .RevertUpdate(offset);
                    }
                }
            }
        }
    });
}

void TransactionManager::GarbageCollect() {
    // Phase 4: deregistration is already done. Full MVCC version GC in Phase 9.
}

size_t TransactionManager::ActiveTransactionCount() const {
    std::lock_guard<std::mutex> lk(mu_);
    return active_transactions_.size();
}

} // namespace cppcoldb
