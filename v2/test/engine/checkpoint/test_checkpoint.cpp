// Placeholder stub for cppcoldb::engine::checkpoint::CheckpointManager and
// RecoveryManager. Also exercises compilation of FakeCheckpointManager,
// FakeRecoveryManager, and FakeTaskScheduler (checkpointing schedules async
// work via ITaskScheduler). No assertions yet.
#include "cppcoldb/engine/checkpoint/checkpoint_manager.hpp"
#include "cppcoldb/engine/checkpoint/recovery_manager.hpp"

#include "helpers/fake_checkpoint_manager.hpp"
#include "helpers/fake_recovery_manager.hpp"
#include "helpers/fake_task_scheduler.hpp"

namespace cppcoldb::test {

void PlaceholderTestCheckpoint() {
    // TODO: add assertions once checkpoint + recovery are implemented.
}

} // namespace cppcoldb::test
