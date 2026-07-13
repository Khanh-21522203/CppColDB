#pragma once

#include <string>
#include <vector>

#include "cppcoldb/common/types/timestamp.hpp"
#include "cppcoldb/common/types/transaction_id.hpp"
#include "cppcoldb/engine/abstractions/catalog/i_schema_store.hpp"
#include "cppcoldb/engine/abstractions/transaction/i_transaction.hpp"

namespace cppcoldb::engine::catalog {

class Schema;
struct CatalogEntry;
struct TableCatalogEntry;
struct ColumnDefinition;

// Abstract catalog: owns named schemas and dispatches MVCC-aware DDL operations
// and entry lookups to them.
class ICatalog {
public:
    virtual ~ICatalog() = default;

    // Schema management.
    virtual void    CreateSchema(const std::string& name, const transaction::ITransaction& tx) = 0;
    virtual void    DropSchema(const std::string& name, const transaction::ITransaction& tx) = 0;
    virtual Schema* GetSchema(const std::string& name) const = 0;

    // Entry lookup (MVCC-aware).
    virtual CatalogEntry* GetEntry(const std::string& schema, const std::string& name,
                                    const transaction::ITransaction& tx,
                                    OnNotFound policy = OnNotFound::THROW) const = 0;
    virtual TableCatalogEntry* GetTable(const std::string& schema, const std::string& name,
                                        const transaction::ITransaction& tx) const = 0;

    // DDL operations.
    virtual void CreateTable(const std::string& schema, const std::string& name,
                              const std::vector<ColumnDefinition>& columns,
                              const transaction::ITransaction& tx) = 0;
    virtual void DropTable(const std::string& schema, const std::string& name,
                            const transaction::ITransaction& tx) = 0;

    // MVCC lifecycle — invoked by the transaction manager on commit/rollback.
    virtual void CommitEntry(const std::string& schema, const std::string& name,
                              common::TransactionId tx_id, common::Timestamp commit_time) = 0;
    virtual void RollbackCreate(const std::string& schema, const std::string& name,
                                 common::TransactionId tx_id) = 0;
    virtual void RollbackDrop(const std::string& schema, const std::string& name,
                               common::TransactionId tx_id) = 0;
};

} // namespace cppcoldb::engine::catalog
