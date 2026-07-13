#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "cppcoldb/common/types/timestamp.hpp"
#include "cppcoldb/common/types/transaction_id.hpp"
#include "cppcoldb/engine/abstractions/transaction/i_transaction.hpp"

namespace cppcoldb::engine::catalog {

struct CatalogEntry;

// Lookup policy when a requested catalog entry does not exist / is not visible
// to the querying transaction.
enum class OnNotFound : std::uint8_t { THROW, RETURN_NULL };

// Abstract single-schema entry store: owns name-scoped, MVCC-aware catalog
// entries (tables, and future views/indexes) and their commit/rollback lifecycle.
class ISchemaStore {
public:
    virtual ~ISchemaStore() = default;

    virtual const std::string& Name() const = 0;

    // MVCC-aware lookup within this schema.
    virtual CatalogEntry* GetEntry(const std::string& name, const transaction::ITransaction& tx,
                                    OnNotFound policy = OnNotFound::THROW) const = 0;

    // Register a newly created entry (uncommitted, owned by the creating transaction).
    virtual void CreateEntry(std::unique_ptr<CatalogEntry> entry) = 0;

    // Mark the visible entry as pending-deleted by tx.
    virtual void MarkDeleted(const std::string& name, const transaction::ITransaction& tx) = 0;

    // Commit: stamp create_commit_time / delete_commit_time for entries owned by tx_id.
    virtual void CommitEntry(const std::string& name, common::TransactionId tx_id,
                             common::Timestamp commit_time) = 0;

    // Rollback a CREATE: remove the entry owned by tx_id.
    virtual void RollbackCreate(const std::string& name, common::TransactionId tx_id) = 0;

    // Rollback a DROP: clear delete markers owned by tx_id.
    virtual void RollbackDrop(const std::string& name, common::TransactionId tx_id) = 0;
};

} // namespace cppcoldb::engine::catalog
