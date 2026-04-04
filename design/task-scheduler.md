# Background Task Scheduler

## Purpose

Run background `Task` objects on worker threads, currently used for optional async checkpoint submission.

## Scope

**In scope:**
- Thread pool startup/shutdown.
- Task queueing and worker loop behavior.
- Exception handling for task execution.

**Out of scope:**
- Task definitions beyond scheduler integration points.
- Scheduling policies beyond FIFO queue behavior.

## Primary User Flow

1. `Database` initializes scheduler via `TaskScheduler::Initialize()`.
2. Components submit tasks with `Submit(std::unique_ptr<Task>)`.
3. Worker thread executes `Task::Execute()`.
4. Shutdown sets stop flag, wakes workers, and joins threads.

## System Flow

1. `Initialize` spawns `num_threads_` worker threads.
2. `Submit` pushes task into `queue_` and notifies one worker.
3. Worker waits on condition variable until queue non-empty or shutdown requested.
4. Worker pops front task and executes inside try/catch block; exceptions are printed to stderr.
5. `Shutdown` marks `shutdown_ = true`, notifies all workers, joins and clears thread list.

## Data Model

- `Task` (`src/parallel/task.hpp`):
- pure virtual `TaskResult Execute()`
- `TaskResult` enum: `FINISHED`, `ERROR`
- `TaskScheduler` (`src/parallel/task_scheduler.hpp`) state:
- `num_threads_`
- `workers_ (vector<thread>)`
- `queue_ (deque<unique_ptr<Task>>)`
- synchronization: `mu_`, `cv_`, `shutdown_`, `initialized_`

## Interfaces and Contracts

- `TaskScheduler::Initialize()`
- contract: idempotent; must run before meaningful submissions.
- `TaskScheduler::Submit(std::unique_ptr<Task>)`
- contract: throws `RuntimeError` when called after shutdown.
- `TaskScheduler::Shutdown()`
- contract: stops accepting work and waits for active workers.
- `CheckpointManager::ScheduleAsyncCheckpoint(TaskScheduler&)`
- contract: submits `AsyncCheckpointTask` wrapper.

## Dependencies

**Internal modules:**
- `src/common/exception.hpp` for submit-after-shutdown error.
- `src/checkpoint/checkpoint_manager.hpp` for async checkpoint task type.

**External services/libraries:**
- C++ threading primitives (`std::thread`, `std::mutex`, `std::condition_variable`, `std::atomic`).

## Failure Modes and Edge Cases

- Submitting after shutdown throws `RuntimeError("TaskScheduler::Submit called after Shutdown()")`.
- Task exceptions are caught and logged to stderr; scheduler continues running.
- There is no retry/dead-letter policy for failed tasks.

## Observability and Debugging

- Only stderr logging in worker exception handlers:
- `[TaskScheduler] task threw: ...`
- Debug points:
- queue and wakeup behavior in `src/parallel/task_scheduler.cpp:WorkerLoop`

## Risks and Notes

- Scheduler is initialized in `Database` but async checkpoint scheduling is not invoked by default lifecycle paths.
- FIFO queue and fixed worker count are simple and predictable but provide no priority/backpressure controls.

Changes:

