#include "cppcoldb/engine/transaction/version_manager.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::transaction {

void VersionManager::RegisterInsert(common::RowId /*row_id*/, common::TransactionId /*tx_id*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void VersionManager::RegisterDelete(common::RowId /*row_id*/, common::TransactionId /*tx_id*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void VersionManager::CommitVersions(common::TransactionId /*tx_id*/, common::Timestamp /*commit_time*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void VersionManager::RollbackVersions(common::TransactionId /*tx_id*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

bool VersionManager::IsVisible(common::RowId /*row_id*/, common::Timestamp /*snapshot_time*/) const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

std::size_t VersionManager::GarbageCollect(common::Timestamp /*oldest_active_snapshot*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::transaction
