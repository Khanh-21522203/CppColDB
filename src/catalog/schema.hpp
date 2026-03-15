#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include "catalog/catalog_entry.hpp"

namespace cppcoldb {

class Transaction;

enum class OnNotFound { THROW, RETURN_NULL };

class Schema {
public:
    explicit Schema(std::string name);

    // MVCC-aware lookup.
    CatalogEntry* GetEntry(const std::string& name, const Transaction& tx,
                           OnNotFound policy = OnNotFound::THROW) const;

    void CreateEntry(std::unique_ptr<CatalogEntry> entry);

    // Mark the visible entry as pending-deleted by tx.
    void MarkDeleted(const std::string& name, const Transaction& tx);

    // Commit: set create_commit_time / delete_commit_time for entries owned by tx_id.
    void CommitEntry(const std::string& name, TransactionId tx_id,
                     timestamp_t commit_time);

    // Rollback a CREATE: remove the entry owned by tx_id.
    void RollbackCreate(const std::string& name, TransactionId tx_id);

    // Rollback a DROP: clear delete markers owned by tx_id.
    void RollbackDrop(const std::string& name, TransactionId tx_id);

    const std::string& Name() const { return name_; }

    // Expose entries for Catalog::GetTable iteration.
    const std::unordered_map<std::string,
          std::vector<std::unique_ptr<CatalogEntry>>>& Entries() const {
        return entries_;
    }

private:
    std::string name_;
    std::unordered_map<std::string,
                       std::vector<std::unique_ptr<CatalogEntry>>> entries_;
};

// Catalog-entry MVCC visibility check.
bool IsCatalogEntryVisible(const CatalogEntry& entry, const Transaction& tx);

} // namespace cppcoldb
