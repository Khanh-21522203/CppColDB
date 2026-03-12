# Checkpoint Flow

## Assumptions
- A checkpoint writes the current in-memory database state to the persistent file, then truncates the WAL.
- Checkpoints are triggered automatically when the WAL exceeds a size threshold, or explicitly.
- A checkpoint lock ensures only one checkpoint runs at a time.
- In-memory databases never checkpoint.

## Diagram

```mermaid
flowchart TD
    A["Checkpoint triggered\n(post-commit or explicit CHECKPOINT)"] --> B{"In-memory\ndatabase?"}
    B -- Yes --> NO["Cannot checkpoint:\nin-memory mode"]
    B -- No --> C{"Acquire checkpoint\nlock (try_lock)"}

    C -- Failed --> NO2["Skip: another checkpoint\nis already running"]
    C -- Acquired --> D{"WAL size\nabove threshold?"}
    D -- No --> NO3["Skip: nothing significant\nto checkpoint"]
    D -- Yes --> E["Acquire WAL write lock\n(pause new WAL writes)"]

    E --> F["Write WAL_CHECKPOINT marker\nto current WAL"]
    F --> G["WAL::Flush() + fsync\nSeal current WAL segment"]

    G --> H["CheckpointManager::\nCreateCheckpoint()"]
    H --> I["Catalog::Serialize(writer)\nWrite schema + table metadata\nto file header block"]
    I --> J["For each Table in Catalog"]
    J --> K["For each RowGroup"]
    K --> L["For each ColumnChunk"]
    L --> M["ColumnChunk::Flush()\nCompress and write ColumnSegments\nto BlockFile via BufferManager"]
    M --> N{"More ColumnChunks\nin RowGroup?"}
    N -- Yes --> L
    N -- No --> O{"More RowGroups\nin Table?"}
    O -- Yes --> K
    O -- No --> P{"More Tables?"}
    P -- Yes --> J
    P -- No --> Q["BlockManager::Flush()\nSync all dirty blocks to disk"]

    Q --> R["WAL::Truncate()\nRemove entries before\ncheckpoint marker"]
    R --> S["Release WAL write lock"]
    S --> T["Release checkpoint lock"]
    T --> U["Checkpoint complete"]
```

## Planned Implementation
- `src/storage/checkpoint_manager.cpp` — CheckpointManager::CreateCheckpoint()
- `src/storage/storage_manager.cpp` — checkpoint trigger logic
- `src/storage/wal.cpp` — WAL::Truncate()
- `src/storage/column/column_chunk.cpp` — ColumnChunk::Flush()
- `src/catalog/catalog.cpp` — Catalog::Serialize()
