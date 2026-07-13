#pragma once

#include <cstddef>
#include <memory>

#include "cppcoldb/common/types/isolation_level.hpp"
#include "cppcoldb/common/types/timestamp.hpp"
#include "cppcoldb/engine/abstractions/transaction/i_transaction.hpp"

namespace cppcoldb::engine::transaction {

// Abstract transaction manager: assigns transaction/commit timestamps and
// drives commit/rollback of a transaction's changes.
class ITransactionManager {
public:
    virtual ~ITransactionManager() = default;

    virtual std::shared_ptr<ITransaction> BeginTransaction(
        bool auto_commit = true,
        common::IsolationLevel isolation = common::IsolationLevel::SNAPSHOT) = 0;

    virtual void Commit(std::shared_ptr<ITransaction> tx) = 0;
    virtual void Rollback(std::shared_ptr<ITransaction> tx) = 0;

    virtual common::Timestamp CurrentCommitTime() const = 0;
    virtual std::size_t       ActiveTransactionCount() const = 0;
};

} // namespace cppcoldb::engine::transaction
