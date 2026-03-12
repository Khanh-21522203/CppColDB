# Task Scheduler Flow

## Assumptions
- CppColDB uses a simple thread pool for background tasks only (checkpointing, compaction).
- Query execution itself is single-threaded; the thread pool is not used for pipeline execution.
- Worker threads pull tasks from a shared task queue.
- Tasks are submitted by the foreground thread (e.g. on commit, trigger async checkpoint).

## Diagram

```mermaid
flowchart TD
    A["TaskScheduler::Initialize(num_threads)"] --> B["Create task_queue\n(mutex-protected deque)"]
    B --> C["Spawn N worker threads"]
    C --> D["Each worker runs\nWorkerThread::Run()"]

    subgraph WorkerLoop["Worker Thread: Run()"]
        W1["Wait on condition variable\n(task_queue not empty or shutdown)"]
        W1 --> W2{"Shutdown\nsignaled?"}
        W2 -- Yes --> W3["Thread exits"]
        W2 -- No --> W4["Dequeue next Task\nfrom task_queue"]
        W4 --> W5{"Got a task?"}
        W5 -- No --> W1
        W5 -- Yes --> W6["Task::Execute()"]
        W6 --> W7{"Task\nresult?"}
        W7 -- FINISHED --> W8["task.reset()\nSignal completion if needed"]
        W7 -- ERROR --> W9["Log error\ntask.reset()"]
        W8 --> W1
        W9 --> W1
    end

    subgraph SubmitTask["Foreground: SubmitTask(task)"]
        ST1["TaskScheduler::Submit(task)"]
        ST1 --> ST2["Lock task_queue mutex"]
        ST2 --> ST3["Push task to queue"]
        ST3 --> ST4["Notify one worker\nvia condition variable"]
        ST4 --> ST5["Unlock mutex"]
    end

    subgraph UseCases["Background Task Use Cases"]
        UC1["AsyncCheckpointTask\n(triggered after commit when WAL is large)"]
        UC2["CompactionTask\n(merge small ColumnSegments into larger ones)"]
        UC3["GarbageCollectionTask\n(clean up old MVCC versions)"]
    end

    subgraph Shutdown["TaskScheduler::Shutdown()"]
        SH1["Set shutdown flag = true"]
        SH1 --> SH2["Notify all workers"]
        SH2 --> SH3["Join all worker threads"]
        SH3 --> SH4["Thread pool stopped"]
    end
```

## Planned Implementation
- `src/parallel/task_scheduler.cpp` — TaskScheduler, worker thread loop
- `src/parallel/task.cpp` — Task base class
- `src/storage/checkpoint_manager.cpp` — AsyncCheckpointTask
