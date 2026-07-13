#include "cppcoldb/engine/catalog/catalog.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::catalog {

void Catalog::CreateSchema(const std::string& /*name*/, const transaction::ITransaction& /*tx*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void Catalog::DropSchema(const std::string& /*name*/, const transaction::ITransaction& /*tx*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

Schema* Catalog::GetSchema(const std::string& /*name*/) const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

CatalogEntry* Catalog::GetEntry(const std::string& /*schema*/, const std::string& /*name*/,
                                 const transaction::ITransaction& /*tx*/,
                                 OnNotFound /*policy*/) const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

TableCatalogEntry* Catalog::GetTable(const std::string& /*schema*/, const std::string& /*name*/,
                                      const transaction::ITransaction& /*tx*/) const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void Catalog::CreateTable(const std::string& /*schema*/, const std::string& /*name*/,
                           const std::vector<ColumnDefinition>& /*columns*/,
                           const transaction::ITransaction& /*tx*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void Catalog::DropTable(const std::string& /*schema*/, const std::string& /*name*/,
                         const transaction::ITransaction& /*tx*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void Catalog::CommitEntry(const std::string& /*schema*/, const std::string& /*name*/,
                           common::TransactionId /*tx_id*/, common::Timestamp /*commit_time*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void Catalog::RollbackCreate(const std::string& /*schema*/, const std::string& /*name*/,
                              common::TransactionId /*tx_id*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void Catalog::RollbackDrop(const std::string& /*schema*/, const std::string& /*name*/,
                            common::TransactionId /*tx_id*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::catalog
