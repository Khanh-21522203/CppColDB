#pragma once
#include <cstddef>

namespace cppcoldb::engine::storage { class IWal; }

namespace cppcoldb::engine::checkpoint {

// v2 addition (no v1 equivalent): replays WAL entries after an unclean
// shutdown to restore committed state before the engine accepts new work.
class IRecoveryManager {
public:
    virtual ~IRecoveryManager() = default;

    // Replay all entries in wal since the last checkpoint marker.
    // Returns the number of entries successfully applied.
    virtual std::size_t Recover(cppcoldb::engine::storage::IWal& wal) = 0;
};

} // namespace cppcoldb::engine::checkpoint
