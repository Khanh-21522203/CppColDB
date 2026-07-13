#include "cppcoldb/engine/transaction/transaction_manager.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::transaction {

TransactionManager::TransactionManager(catalog::ICatalog& catalog, IVersionManager& version_manager)
    : catalog_(catalog), version_manager_(version_manager) {}

std::shared_ptr<ITransaction> TransactionManager::BeginTransaction(
    bool /*auto_commit*/, common::IsolationLevel /*isolation*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void TransactionManager::Commit(std::shared_ptr<ITransaction> /*tx*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void TransactionManager::Rollback(std::shared_ptr<ITransaction> /*tx*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

common::Timestamp TransactionManager::CurrentCommitTime() const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

std::size_t TransactionManager::ActiveTransactionCount() const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void TransactionManager::ApplyUndoBuffer(Transaction& /*tx*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void TransactionManager::UndoBufferReverse(Transaction& /*tx*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void TransactionManager::GarbageCollect() {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::transaction
