#pragma once
#include <cstddef>

#include "cppcoldb/common/not_implemented.hpp"
#include "cppcoldb/engine/abstractions/checkpoint/i_recovery_manager.hpp"

namespace cppcoldb::test {

// Compile-time stand-in for IRecoveryManager. Not executed; every method throws.
class FakeRecoveryManager final : public engine::checkpoint::IRecoveryManager {
public:
    std::size_t Recover(engine::storage::IWal& wal) override { CPPCOLDB_NOT_IMPLEMENTED(); }
};

} // namespace cppcoldb::test
