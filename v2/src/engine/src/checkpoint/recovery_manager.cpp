#include "cppcoldb/engine/checkpoint/recovery_manager.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::checkpoint {

std::size_t RecoveryManager::Recover(cppcoldb::engine::storage::IWal& /*wal*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

} // namespace cppcoldb::engine::checkpoint
