#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "cppcoldb/common/types/timestamp.hpp"
#include "cppcoldb/common/types/transaction_id.hpp"
#include "cppcoldb/engine/abstractions/catalog/i_schema_store.hpp"
#include "cppcoldb/engine/abstractions/transaction/i_transaction.hpp"
#include "cppcoldb/engine/catalog/catalog_entry.hpp"

namespace cppcoldb::engine::catalog {

// Concrete, in-memory, MVCC-aware schema: owns the named catalog entries
// (and their historical versions) that make up one namespace.
class Schema final : public ISchemaStore {
public:
    explicit Schema(std::string name) : name_(std::move(name)) {}
    ~Schema() override = default;

    const std::string& Name() const override { return name_; }

    CatalogEntry* GetEntry(const std::string& name, const transaction::ITransaction& tx,
                            OnNotFound policy = OnNotFound::THROW) const override;
    void CreateEntry(std::unique_ptr<CatalogEntry> entry) override;
    void MarkDeleted(const std::string& name, const transaction::ITransaction& tx) override;

    void CommitEntry(const std::string& name, common::TransactionId tx_id,
                      common::Timestamp commit_time) override;
    void RollbackCreate(const std::string& name, common::TransactionId tx_id) override;
    void RollbackDrop(const std::string& name, common::TransactionId tx_id) override;

    // Expose entries (including superseded versions) for Catalog iteration.
    const std::unordered_map<std::string, std::vector<std::unique_ptr<CatalogEntry>>>&
    Entries() const {
        return entries_;
    }

private:
    std::string name_;
    std::unordered_map<std::string, std::vector<std::unique_ptr<CatalogEntry>>> entries_;
};

// Catalog-entry MVCC visibility check: is entry visible to tx's snapshot?
bool IsCatalogEntryVisible(const CatalogEntry& entry, const transaction::ITransaction& tx);

} // namespace cppcoldb::engine::catalog
