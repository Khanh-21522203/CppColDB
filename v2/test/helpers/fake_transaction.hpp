#pragma once

#include "cppcoldb/common/not_implemented.hpp"
#include "cppcoldb/engine/abstractions/transaction/i_transaction.hpp"

namespace cppcoldb::test {

// Compile-time stand-in for ITransaction. Not executed; every method throws.
class FakeTransaction final : public engine::transaction::ITransaction {
public:
    common::TransactionId  Id() const override { CPPCOLDB_NOT_IMPLEMENTED(); }
    common::Timestamp      StartTime() const override { CPPCOLDB_NOT_IMPLEMENTED(); }
    common::Timestamp      CommitTime() const override { CPPCOLDB_NOT_IMPLEMENTED(); }
    common::IsolationLevel Isolation() const override { CPPCOLDB_NOT_IMPLEMENTED(); }
    bool                   IsAutoCommit() const override { CPPCOLDB_NOT_IMPLEMENTED(); }
    bool                   IsInvalid() const override { CPPCOLDB_NOT_IMPLEMENTED(); }
};

} // namespace cppcoldb::test
