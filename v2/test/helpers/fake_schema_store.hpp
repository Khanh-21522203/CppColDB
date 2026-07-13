#pragma once
#include <memory>
#include <string>

#include "cppcoldb/common/not_implemented.hpp"
#include "cppcoldb/engine/abstractions/catalog/i_schema_store.hpp"

namespace cppcoldb::test {

// Compile-time stand-in for ISchemaStore. Not executed; every method throws.
class FakeSchemaStore final : public engine::catalog::ISchemaStore {
public:
    const std::string& Name() const override { CPPCOLDB_NOT_IMPLEMENTED(); }

    engine::catalog::CatalogEntry* GetEntry(
        const std::string& name, const engine::transaction::ITransaction& tx,
        engine::catalog::OnNotFound policy = engine::catalog::OnNotFound::THROW) const override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }

    void CreateEntry(std::unique_ptr<engine::catalog::CatalogEntry> entry) override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }

    void MarkDeleted(const std::string& name, const engine::transaction::ITransaction& tx) override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }

    void CommitEntry(const std::string& name, common::TransactionId tx_id,
                      common::Timestamp commit_time) override { CPPCOLDB_NOT_IMPLEMENTED(); }
    void RollbackCreate(const std::string& name, common::TransactionId tx_id) override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }
    void RollbackDrop(const std::string& name, common::TransactionId tx_id) override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }
};

} // namespace cppcoldb::test
