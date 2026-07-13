#pragma once
#include <string>
#include <vector>

#include "cppcoldb/common/not_implemented.hpp"
#include "cppcoldb/engine/abstractions/catalog/i_catalog.hpp"

namespace cppcoldb::test {

// Compile-time stand-in for ICatalog. Not executed; every method throws.
class FakeCatalog final : public engine::catalog::ICatalog {
public:
    void CreateSchema(const std::string& name, const engine::transaction::ITransaction& tx) override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }
    void DropSchema(const std::string& name, const engine::transaction::ITransaction& tx) override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }
    engine::catalog::Schema* GetSchema(const std::string& name) const override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }

    engine::catalog::CatalogEntry* GetEntry(
        const std::string& schema, const std::string& name,
        const engine::transaction::ITransaction& tx,
        engine::catalog::OnNotFound policy = engine::catalog::OnNotFound::THROW) const override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }
    engine::catalog::TableCatalogEntry* GetTable(const std::string& schema, const std::string& name,
                                                  const engine::transaction::ITransaction& tx) const override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }

    void CreateTable(const std::string& schema, const std::string& name,
                      const std::vector<engine::catalog::ColumnDefinition>& columns,
                      const engine::transaction::ITransaction& tx) override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }
    void DropTable(const std::string& schema, const std::string& name,
                    const engine::transaction::ITransaction& tx) override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }

    void CommitEntry(const std::string& schema, const std::string& name,
                      common::TransactionId tx_id, common::Timestamp commit_time) override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }
    void RollbackCreate(const std::string& schema, const std::string& name,
                         common::TransactionId tx_id) override { CPPCOLDB_NOT_IMPLEMENTED(); }
    void RollbackDrop(const std::string& schema, const std::string& name,
                       common::TransactionId tx_id) override { CPPCOLDB_NOT_IMPLEMENTED(); }
};

} // namespace cppcoldb::test
