#pragma once

#include "cppcoldb/engine/abstractions/checkpoint/i_recovery_manager.hpp"

namespace cppcoldb::engine::checkpoint {

// v2 addition (no v1 equivalent): replays WAL entries after an unclean
// shutdown to restore committed state before the engine accepts new work.
class RecoveryManager final : public IRecoveryManager {
public:
    RecoveryManager() = default;
    ~RecoveryManager() override = default;

    std::size_t Recover(cppcoldb::engine::storage::IWal& wal) override;
};

} // namespace cppcoldb::engine::checkpoint
