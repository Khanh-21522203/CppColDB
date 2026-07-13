#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "cppcoldb/common/types/timestamp.hpp"
#include "cppcoldb/common/types/transaction_id.hpp"
#include "cppcoldb/engine/abstractions/catalog/i_catalog.hpp"
#include "cppcoldb/engine/abstractions/transaction/i_transaction.hpp"
#include "cppcoldb/engine/catalog/schema.hpp"

namespace cppcoldb::engine::catalog {

// Concrete in-memory catalog: owns all schemas and dispatches MVCC-aware
// lookups and DDL operations to them.
class Catalog final : public ICatalog {
public:
    static constexpr const char* DEFAULT_SCHEMA = "main";

    Catalog() = default;
    ~Catalog() override = default;

    void    CreateSchema(const std::string& name, const transaction::ITransaction& tx) override;
    void    DropSchema(const std::string& name, const transaction::ITransaction& tx) override;
    Schema* GetSchema(const std::string& name) const override;

    CatalogEntry* GetEntry(const std::string& schema, const std::string& name,
                            const transaction::ITransaction& tx,
                            OnNotFound policy = OnNotFound::THROW) const override;
    TableCatalogEntry* GetTable(const std::string& schema, const std::string& name,
                                 const transaction::ITransaction& tx) const override;

    void CreateTable(const std::string& schema, const std::string& name,
                      const std::vector<ColumnDefinition>& columns,
                      const transaction::ITransaction& tx) override;
    void DropTable(const std::string& schema, const std::string& name,
                    const transaction::ITransaction& tx) override;

    void CommitEntry(const std::string& schema, const std::string& name,
                      common::TransactionId tx_id, common::Timestamp commit_time) override;
    void RollbackCreate(const std::string& schema, const std::string& name,
                         common::TransactionId tx_id) override;
    void RollbackDrop(const std::string& schema, const std::string& name,
                       common::TransactionId tx_id) override;

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, std::unique_ptr<Schema>> schemas_;
};

} // namespace cppcoldb::engine::catalog
