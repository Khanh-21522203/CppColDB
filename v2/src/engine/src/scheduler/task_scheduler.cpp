#include "cppcoldb/engine/scheduler/task_scheduler.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::scheduler {

TaskScheduler::TaskScheduler(std::size_t num_threads) : num_threads_(num_threads) {}

void TaskScheduler::Initialize() { CPPCOLDB_NOT_IMPLEMENTED(); }

void TaskScheduler::Submit(std::unique_ptr<ITask> /*task*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void TaskScheduler::Shutdown() { CPPCOLDB_NOT_IMPLEMENTED(); }

void TaskScheduler::WorkerLoop() { CPPCOLDB_NOT_IMPLEMENTED(); }

} // namespace cppcoldb::engine::scheduler
