# Transaction Commit Flow

## Assumptions
- Commit writes the WAL first, then applies local changes to main storage, then updates version timestamps.
- After WAL fsync, the transaction is durable even if a crash occurs before the storage update.
- An optional checkpoint may be triggered after commit if the WAL is large enough.

## Diagram

```mermaid
flowchart TD
    A["TransactionManager::\nCommit(transaction)"] --> B{"Transaction made\nany changes?"}
    B -- No changes --> C["Read-only commit:\nskip WAL write"]
    B -- Yes --> D["--- Step 1: Write WAL ---"]

    D --> E{"In-memory\ndatabase?"}
    E -- Yes --> F["Skip WAL\n(no persistence needed)"]
    E -- No --> G["Acquire WAL write lock"]
    G --> H["Transaction::WriteToWAL(wal)\n(serialize UndoBuffer entries)"]
    H --> I["WAL::Flush() + fsync\n(durable on disk before proceeding)"]
    I --> J{"WAL write\nfailed?"}
    J -- Yes --> K["Rollback(transaction)\nReturn error"]
    J -- No --> L["Release WAL write lock"]

    C --> M["--- Step 2: Assign commit_time ---"]
    F --> M
    L --> M
    M --> N["commit_time = ++commit_counter\n(global monotonic counter)"]

    N --> O["--- Step 3: Apply UndoBuffer to main storage ---"]
    O --> P["For each entry in UndoBuffer"]
    P --> Q{"Entry type?"}
    Q -- CATALOG_ENTRY --> R["Update CatalogEntry timestamps\n(entry.commit_time = commit_time)"]
    Q -- INSERT_ROWS --> S["RowGroup::CommitAppend(commit_time)\nRows become visible to other transactions"]
    Q -- DELETE_ROWS --> T["VersionInfo::CommitDelete(commit_time)\nRows become invisible after commit_time"]
    Q -- UPDATE_ROWS --> U["VersionInfo::CommitUpdate(commit_time)\nNew values visible after commit_time"]

    R --> V["Next entry"]
    S --> V
    T --> V
    U --> V
    V --> P

    P --> W["All UndoBuffer entries applied"]
    W --> X["Remove transaction\nfrom active_transactions"]
    X --> Y["Schedule garbage collection\nof old versions no longer needed"]

    Y --> Z["--- Step 4: Optional Checkpoint ---"]
    Z --> AA{"WAL size\n> checkpoint threshold?"}
    AA -- Yes --> AB["Submit AsyncCheckpointTask\nto TaskScheduler"]
    AA -- No --> AC["Done"]
    AB --> AC
```

## Planned Implementation
- `src/transaction/transaction_manager.cpp` — TransactionManager::Commit()
- `src/transaction/transaction.cpp` — Transaction::WriteToWAL(), UndoBuffer traversal
- `src/storage/wal.cpp` — WAL::Flush()
- `src/storage/column/row_group.cpp` — RowGroup::CommitAppend()
- `src/storage/column/version_info.cpp` — VersionInfo::CommitDelete(), CommitUpdate()
