# Database Initialization Flow

## Assumptions
- Database constructor is the primary entry point.
- Two paths: in-memory database (no file) and persistent file database.
- On startup with an existing file, the WAL is replayed for crash recovery.
- Catalog is loaded from the checkpoint file before WAL replay.

## Diagram

```mermaid
flowchart TD
    A["Database(path, config)"] --> B{"Path provided?"}

    B -- "No / ':memory:'" --> C["In-Memory Mode"]
    B -- "Yes (file path)" --> D["Persistent Mode"]

    C --> C1["Initialize empty Catalog"]
    C1 --> C2["Initialize BufferManager\n(memory-only, no BlockFile)"]
    C2 --> C3["Initialize TransactionManager"]
    C3 --> C4["Database Ready\n(no WAL, no disk I/O)"]

    D --> D1["Initialize BufferManager\nwith BlockFile"]
    D1 --> D2{"File exists?"}

    D2 -- No --> D3["Create new database file"]
    D3 --> D4["Initialize empty Catalog"]
    D4 --> D5["Initialize StorageManager"]
    D5 --> D6["Create fresh WAL"]
    D6 --> READY["Database Ready"]

    D2 -- Yes --> D7["Open existing database file"]
    D7 --> D8["Read checkpoint block\nfrom file header"]
    D8 --> D9["Deserialize Catalog\nfrom checkpoint"]
    D9 --> D10["Load RowGroup metadata\nfrom checkpoint"]
    D10 --> D11["Initialize StorageManager\nwith loaded state"]
    D11 --> D12{"WAL file exists?"}

    D12 -- No --> READY
    D12 -- Yes --> D13["Open WAL for replay"]
    D13 --> D14["Read WAL entries in order"]
    D14 --> D15{"Entry past last\ncheckpoint?"}
    D15 -- No --> D16["Skip (already checkpointed)"]
    D15 -- Yes --> D17{"Entry type?"}

    D16 --> D14
    D17 -- "CREATE TABLE/SCHEMA" --> D18["Apply to Catalog"]
    D17 -- "DROP TABLE" --> D18
    D17 -- "INSERT" --> D19["Apply to RowGroup storage"]
    D17 -- "DELETE / UPDATE" --> D19
    D18 --> D20["Next entry"]
    D19 --> D20
    D20 --> D14

    D14 --> D21{"All entries\nreplayed?"}
    D21 -- Yes --> D22["Checkpoint recovered state"]
    D22 --> D23["Truncate / rotate WAL"]
    D23 --> READY

    READY --> Z["TransactionManager initialized\nConnections can be opened"]
```

## Planned Implementation
- `src/main/database.cpp` — Database constructor, init sequence
- `src/storage/storage_manager.cpp` — StorageManager::Load()
- `src/storage/wal_replay.cpp` — WAL replay logic
- `src/catalog/catalog.cpp` — Catalog deserialization
