#pragma once
#include <cstddef>
#include <memory>

#include "cppcoldb/common/not_implemented.hpp"
#include "cppcoldb/engine/abstractions/transaction/i_transaction_manager.hpp"

namespace cppcoldb::test {

// Compile-time stand-in for ITransactionManager. Not executed; every method throws.
class FakeTransactionManager final : public engine::transaction::ITransactionManager {
public:
    std::shared_ptr<engine::transaction::ITransaction> BeginTransaction(
        bool auto_commit = true,
        common::IsolationLevel isolation = common::IsolationLevel::SNAPSHOT) override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }

    void Commit(std::shared_ptr<engine::transaction::ITransaction> tx) override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }
    void Rollback(std::shared_ptr<engine::transaction::ITransaction> tx) override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }

    common::Timestamp CurrentCommitTime() const override { CPPCOLDB_NOT_IMPLEMENTED(); }
    std::size_t       ActiveTransactionCount() const override { CPPCOLDB_NOT_IMPLEMENTED(); }
};

} // namespace cppcoldb::test
