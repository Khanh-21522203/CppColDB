#pragma once

#include "cppcoldb/common/types/isolation_level.hpp"
#include "cppcoldb/common/types/timestamp.hpp"
#include "cppcoldb/common/types/transaction_id.hpp"

namespace cppcoldb::engine::transaction {

// Abstract transaction handle: identity, MVCC snapshot bounds, and lifecycle
// flags visible to the rest of the engine (catalog, execution, storage).
class ITransaction {
public:
    virtual ~ITransaction() = default;

    virtual common::TransactionId  Id() const = 0;
    virtual common::Timestamp      StartTime() const = 0;
    virtual common::Timestamp      CommitTime() const = 0;
    virtual common::IsolationLevel Isolation() const = 0;
    virtual bool                   IsAutoCommit() const = 0;
    virtual bool                   IsInvalid() const = 0;
};

} // namespace cppcoldb::engine::transaction
