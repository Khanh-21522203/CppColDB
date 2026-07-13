#pragma once

#include <cstddef>

#include "cppcoldb/common/types/row_id.hpp"
#include "cppcoldb/common/types/timestamp.hpp"
#include "cppcoldb/common/types/transaction_id.hpp"

namespace cppcoldb::engine::transaction {

// Abstract MVCC version manager: a v2 addition that consolidates per-row
// version bookkeeping (who created/deleted a row and when) independently of
// the physical storage layout, so visibility checks do not need to reach into
// the storage layer directly.
class IVersionManager {
public:
    virtual ~IVersionManager() = default;

    // Register that row_id was created by tx_id (uncommitted).
    virtual void RegisterInsert(common::RowId row_id, common::TransactionId tx_id) = 0;

    // Register that row_id was deleted by tx_id (uncommitted).
    virtual void RegisterDelete(common::RowId row_id, common::TransactionId tx_id) = 0;

    // Stamp commit_time on all versions owned by tx_id.
    virtual void CommitVersions(common::TransactionId tx_id, common::Timestamp commit_time) = 0;

    // Discard all versions owned by tx_id (undo insert/delete markers).
    virtual void RollbackVersions(common::TransactionId tx_id) = 0;

    // MVCC snapshot visibility check for a row version.
    virtual bool IsVisible(common::RowId row_id, common::Timestamp snapshot_time) const = 0;

    // Reclaim version metadata not visible to any active snapshot older than
    // oldest_active_snapshot. Returns the number of entries reclaimed.
    virtual std::size_t GarbageCollect(common::Timestamp oldest_active_snapshot) = 0;
};

} // namespace cppcoldb::engine::transaction
