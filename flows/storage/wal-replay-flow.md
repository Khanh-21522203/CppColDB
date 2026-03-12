# WAL Replay Flow

## Assumptions
- WAL replay occurs on startup when an existing database file has a WAL file present.
- Only entries past the last checkpoint marker are replayed; earlier entries are already in the file.
- A truncated or corrupted entry at the end of the WAL is treated as a crash during write and replay stops safely.
- After successful replay, a checkpoint is triggered to compact recovered state back to the main file.

## Diagram

```mermaid
flowchart TD
    A["StorageManager::Load()\ndetects existing WAL file"] --> B["WAL::OpenForReplay()"]
    B --> C["Read WAL entries from start"]

    C --> D["Read next entry header\n[entry_type: uint8][size: uint32]"]
    D --> E{"Valid header\n(not truncated)?"}
    E -- No --> F["Stop replay at last\nvalid entry (partial recovery)"]
    E -- Yes --> G{"Entry type?"}

    G -- WAL_CHECKPOINT --> H["Checkpoint marker:\nskip all entries before this"]
    H --> I["Update last_checkpoint_pos"]
    I --> D

    G -- WAL_CREATE_TABLE --> J["Replay: Catalog::CreateTable()"]
    G -- WAL_DROP_TABLE --> K["Replay: Catalog::DropTable()"]
    G -- WAL_CREATE_SCHEMA --> L["Replay: Catalog::CreateSchema()"]
    G -- WAL_DROP_SCHEMA --> M["Replay: Catalog::DropSchema()"]
    G -- WAL_INSERT --> N["Replay: append rows\nto RowGroup storage"]
    G -- WAL_DELETE --> O["Replay: mark rows\nas deleted in version info"]
    G -- WAL_UPDATE --> P["Replay: apply updates\nto RowGroup storage"]

    J --> Q["Mark entry as replayed"]
    K --> Q
    L --> Q
    M --> Q
    N --> Q
    O --> Q
    P --> Q

    Q --> R{"End of WAL\nreached?"}
    R -- No --> D
    R -- Yes --> S["WAL replay complete"]
    F --> S

    S --> T{"Any entries\nwere replayed?"}
    T -- Yes --> U["CheckpointManager::\nCreateCheckpoint()\nFlush recovered state to main file"]
    U --> V["WAL::Truncate()\nRemove replayed entries"]
    V --> W["Create fresh WAL for new writes"]
    T -- No --> W

    W --> X["Database load complete"]

    subgraph ErrorHandling["Replay Error Handling"]
        EH1["Read error on entry data"]
        EH1 --> EH2{"Truncated at\nend of file?"}
        EH2 -- Yes --> EH3["Assume crash mid-write\nStop replay safely"]
        EH2 -- No --> EH4["Throw IOError:\nWAL file corrupted"]
    end
```

## Planned Implementation
- `src/storage/wal_replay.cpp` — WAL replay loop, entry dispatch
- `src/storage/storage_manager.cpp` — StorageManager::Load(), WAL detection
- `src/storage/wal.cpp` — WAL::OpenForReplay(), ReadEntry(), Truncate()
