#pragma once
#include <cstddef>

#include "cppcoldb/common/not_implemented.hpp"
#include "cppcoldb/engine/abstractions/transaction/i_version_manager.hpp"

namespace cppcoldb::test {

// Compile-time stand-in for IVersionManager. Not executed; every method throws.
class FakeVersionManager final : public engine::transaction::IVersionManager {
public:
    void RegisterInsert(common::RowId row_id, common::TransactionId tx_id) override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }
    void RegisterDelete(common::RowId row_id, common::TransactionId tx_id) override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }
    void CommitVersions(common::TransactionId tx_id, common::Timestamp commit_time) override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }
    void RollbackVersions(common::TransactionId tx_id) override { CPPCOLDB_NOT_IMPLEMENTED(); }

    bool IsVisible(common::RowId row_id, common::Timestamp snapshot_time) const override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }

    std::size_t GarbageCollect(common::Timestamp oldest_active_snapshot) override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }
};

} // namespace cppcoldb::test
