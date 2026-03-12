# Feature: Task Scheduler

## 1. Purpose

The Task Scheduler provides a simple fixed-size thread pool for running background tasks — specifically async checkpointing, column segment compaction, and MVCC garbage collection. Query execution itself is single-threaded; the thread pool is used only for work that can proceed off the critical query path. Worker threads pull tasks from a shared queue protected by a condition variable, execute them, and loop back to wait.

## 2. Responsibilities

- `TaskScheduler::Initialize(num_threads)`: spawn N worker threads; each runs the `WorkerLoop()`
- `TaskScheduler::Submit(task)`: enqueue a `Task*` onto the task queue; notify one waiting worker
- `TaskScheduler::Shutdown()`: set a shutdown flag; notify all workers to wake up; `join()` all threads
- `WorkerLoop()`: wait on the condition variable; dequeue and execute one `Task` per iteration; handle errors by logging and continuing
- Define the `Task` abstract base class with a single `Execute()` virtual method returning `TaskResult`
- Define concrete task types: `AsyncCheckpointTask`, `CompactionTask`, `GarbageCollectionTask`
- Prevent task submission after `Shutdown()` has been called

## 3. Non-Responsibilities

- Does not schedule query execution (queries are single-threaded per connection)
- Does not implement priority scheduling (FIFO order only)
- Does not retry failed tasks
- Does not limit parallelism within a task (tasks are not internally parallel)
- Does not provide a future/promise mechanism for waiting on task results

## 4. Architecture Design

```
TransactionManager::Commit()
  → wal size > threshold
  → TaskScheduler::Submit(AsyncCheckpointTask)

Foreground thread                     Worker threads (N)
       |                              |         |
       | Submit(task)                 |         |
       v                              v         v
 +-----------------+          +--------------+
 |   task_queue_   | <---pop--| WorkerLoop() |
 | (deque<Task*>)  |          |  condition   |
 +-----------------+          |  variable    |
       |                      +--------------+
  notify_one()
```

**Worker shutdown**: on `Shutdown()`, the `shutdown_flag_` is set to true and `notify_all()` is called. Workers that are waiting on the condition variable wake up, check the flag, and exit. Workers currently executing a task finish their current task first.

## 5. Core Data Structures (C++)

```cpp
// src/parallel/task.hpp
#pragma once

namespace cppcoldb {

class Task {
public:
    enum class TaskResult { FINISHED, ERROR };
    virtual TaskResult Execute() = 0;
    virtual ~Task() = default;
};

} // namespace cppcoldb
```

```cpp
// src/parallel/task_scheduler.hpp
#pragma once
#include <deque>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include "parallel/task.hpp"

namespace cppcoldb {

class TaskScheduler {
public:
    explicit TaskScheduler(size_t num_threads = 1);
    ~TaskScheduler(); // calls Shutdown() if not already called

    // Start worker threads.
    void Initialize();

    // Submit a task for background execution.
    // Ownership of the task is transferred to the scheduler.
    // Throws RuntimeError if called after Shutdown().
    void Submit(std::unique_ptr<Task> task);

    // Stop accepting new tasks; wait for running tasks to finish.
    void Shutdown();

    bool IsShutdown() const { return shutdown_.load(); }

private:
    void WorkerLoop();

    size_t                            num_threads_;
    std::vector<std::thread>          workers_;
    std::deque<std::unique_ptr<Task>> queue_;
    std::mutex                        mu_;
    std::condition_variable           cv_;
    std::atomic<bool>                 shutdown_{false};
    bool                              initialized_ = false;
};

} // namespace cppcoldb
```

```cpp
// src/storage/checkpoint_manager.hpp (task definition)
namespace cppcoldb {

class AsyncCheckpointTask : public Task {
public:
    explicit AsyncCheckpointTask(CheckpointManager& mgr) : mgr_(mgr) {}
    TaskResult Execute() override {
        bool ran = mgr_.CreateCheckpoint();
        return TaskResult::FINISHED;
    }
private:
    CheckpointManager& mgr_;
};

// Placeholder for future compaction and GC tasks.
class CompactionTask : public Task {
public:
    TaskResult Execute() override;
};

class GarbageCollectionTask : public Task {
public:
    explicit GarbageCollectionTask(TransactionManager& txn_mgr) : txn_mgr_(txn_mgr) {}
    TaskResult Execute() override;
private:
    TransactionManager& txn_mgr_;
};

} // namespace cppcoldb
```

## 6. Public Interfaces

```cpp
namespace cppcoldb {

class TaskScheduler {
public:
    explicit TaskScheduler(size_t num_threads = 1);

    void Initialize();
    void Submit(std::unique_ptr<Task> task);
    void Shutdown();

    bool IsShutdown() const;
};

class Task {
public:
    enum class TaskResult { FINISHED, ERROR };
    virtual TaskResult Execute() = 0;
    virtual ~Task() = default;
};

} // namespace cppcoldb
```

## 7. Internal Algorithms

### Initialize
```
Initialize():
  initialized_ = true
  for i in 0..num_threads_:
    workers_.push_back(std::thread([this]{ WorkerLoop(); }))
  Time: O(num_threads)
```

### Submit
```
Submit(task):
  if shutdown_.load(): throw RuntimeError("TaskScheduler is shut down")
  lock(mu_)
  queue_.push_back(move(task))
  cv_.notify_one()    // wake up one waiting worker
  unlock
  Time: O(1)
```

### WorkerLoop
```
WorkerLoop():
  while true:
    lock(mu_)
    cv_.wait(mu_, predicate: shutdown_ OR not queue_.empty())

    if shutdown_ AND queue_.empty():
      unlock
      return   // clean exit

    if queue_.empty():
      unlock
      continue  // spurious wake-up

    task = move(queue_.front())
    queue_.pop_front()
    unlock

    try:
      result = task->Execute()
      if result == ERROR:
        // Log error; task is discarded
    catch (std::exception& e):
      // Log exception; continue loop

    task.reset()  // free task memory
  Time per task: O(task.Execute() time)
```

### Shutdown
```
Shutdown():
  shutdown_.store(true)
  cv_.notify_all()           // wake all workers
  for thread in workers_:
    thread.join()            // wait for each to finish current task and exit
  workers_.clear()
  Time: O(num_threads + max_task_time)
```

## 8. Persistence Model

Not applicable. The task scheduler is a runtime-only component. Task state (e.g., checkpoint progress) is managed by the tasks themselves.

## 9. Concurrency Model

| Object | Lock | Usage |
|--------|------|-------|
| `TaskScheduler.mu_` | `std::mutex` | Guards `queue_` for enqueue and dequeue |
| `TaskScheduler.cv_` | `std::condition_variable` | Workers wait; notified on Submit or Shutdown |
| `shutdown_` | `std::atomic<bool>` | Read without lock by Submit; set under condition variable |

**Task safety**: each `Task::Execute()` is responsible for its own internal locking when it accesses shared subsystems (e.g., `CheckpointManager` acquires `checkpoint_lock_` internally). The scheduler does not hold `mu_` while a task executes.

**No task starvation**: tasks are dequeued in FIFO order. Workers process one task at a time; a slow task does not starve other tasks on a multi-worker scheduler, only on a single-worker scheduler.

## 10. Configuration

```cpp
struct TaskSchedulerConfig {
    size_t num_threads = 1; // number of background worker threads
    // For most workloads (checkpoint + occasional GC), 1 thread is sufficient.
    // Increase to 2-4 for workloads with frequent compaction.
};
```

## 11. Testing Strategy

- `TestSubmitAndExecute`: submit a task that sets a flag; verify flag is set after a short wait
- `TestMultipleTasksExecuteInOrder`: submit 5 tasks counting 1..5; verify execution order with single worker
- `TestShutdownWaitsForRunningTask`: submit a slow task, call Shutdown(); verify task completes before Shutdown() returns
- `TestShutdownDrainsQueue`: submit 10 tasks, call Shutdown() immediately; verify all tasks execute
- `TestSubmitAfterShutdown`: call Shutdown(), then Submit() → throws RuntimeError
- `TestMultipleWorkers`: 3 workers, 6 tasks; verify all 6 execute concurrently-ish
- `TestTaskErrorContinues`: task that returns ERROR; verify next task still runs
- `TestTaskExceptionContinues`: task that throws; verify worker loop continues
- `TestShutdownSignalsAllWorkers`: N workers all waiting; Shutdown() causes all to exit
- `TestInitializeIdempotent`: calling Initialize() twice does not start duplicate threads

## 12. Open Questions

- Task cancellation: there is currently no way to cancel an enqueued task. If `Shutdown()` is called with a long-running task in progress, it must complete. A cancellation token pattern could be added later.
- Task deduplication: if multiple async checkpoint tasks are queued (e.g., bursts of commits), all will run. The `CheckpointManager.try_lock()` already handles this gracefully (second task will find the lock held and skip). No deduplication needed at the scheduler level.
