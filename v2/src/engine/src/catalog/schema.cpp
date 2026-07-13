#include "cppcoldb/engine/catalog/schema.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::catalog {

CatalogEntry* Schema::GetEntry(const std::string& /*name*/, const transaction::ITransaction& /*tx*/,
                                OnNotFound /*policy*/) const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void Schema::CreateEntry(std::unique_ptr<CatalogEntry> /*entry*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void Schema::MarkDeleted(const std::string& /*name*/, const transaction::ITransaction& /*tx*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void Schema::CommitEntry(const std::string& /*name*/, common::TransactionId /*tx_id*/,
                          common::Timestamp /*commit_time*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void Schema::RollbackCreate(const std::string& /*name*/, common::TransactionId /*tx_id*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void Schema::RollbackDrop(const std::string& /*name*/, common::TransactionId /*tx_id*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

bool IsCatalogEntryVisible(const CatalogEntry& /*entry*/, const transaction::ITransaction& /*tx*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::catalog
