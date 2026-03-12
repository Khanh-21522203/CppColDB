# MVCC Visibility Flow

## Assumptions
- CppColDB uses Multi-Version Concurrency Control (MVCC) with snapshot isolation.
- Each transaction sees a snapshot based on its start_time: all data committed before start_time is visible.
- Version information for rows is stored in VersionInfo structures linked to RowGroups.
- Uncommitted changes are tracked in the UndoBuffer and visible only to the transaction that made them.
- Garbage collection removes old versions when no active transaction needs them.

## Diagram

```mermaid
flowchart TD
    A["Transaction T starts\n(tx_id = ID, start_time = S)"] --> B["Snapshot: T sees all data\ncommitted before time S"]

    B --> C["--- READ PATH ---"]
    C --> D["Scan row in RowGroup"]
    D --> E["Check row's VersionInfo"]

    E --> F{"VersionInfo\npresent?"}
    F -- No --> G["Row unmodified and committed\n→ visible to all transactions"]
    F -- Yes --> H["Check version entry"]

    H --> I{"Entry type?"}

    I -- "Delete marker" --> J{"delete.commit_time\n< T.start_time?"}
    J -- Yes --> K["Row deleted before T started\n→ invisible (skip row)"]
    J -- No --> L{"delete.tx_id\n== T.tx_id?"}
    L -- Yes --> M["T's own delete\n→ invisible to T"]
    L -- No --> N["Delete uncommitted or\ncommitted after T started\n→ row is visible"]

    I -- "Insert marker" --> O{"insert.commit_time\n< T.start_time?"}
    O -- Yes --> P["Row inserted before T\n→ visible"]
    O -- No --> Q{"insert.tx_id\n== T.tx_id?"}
    Q -- Yes --> R["T's own insert\n→ visible"]
    Q -- No --> S["Insert not yet visible to T\n→ skip row"]

    I -- "Update marker" --> T2["Traverse UndoBuffer version chain\nto find version visible to T"]
    T2 --> U{"Found version with\ncommit_time < T.start_time?"}
    U -- Yes --> V["Use that version's\ncolumn values"]
    U -- No --> W{"Is it T's\nown update?"}
    W -- Yes --> X["Use T's current\nin-progress values"]
    W -- No --> Y["Use original pre-update\nvalues (no version visible)"]

    subgraph GarbageCollection["Version Garbage Collection"]
        GC1["After commit or rollback:\nTransactionManager updates active_transactions"]
        GC1 --> GC2["Compute min_active_start_time\n= lowest start_time of any active transaction"]
        GC2 --> GC3["For each committed transaction\nwith commit_time < min_active_start_time"]
        GC3 --> GC4["No active transaction\ncan see these old versions"]
        GC4 --> GC5["Safe to delete UndoBuffer\nentries for this transaction"]
        GC5 --> GC6["Free UndoBuffer memory"]
    end

    subgraph WriteConflict["Write-Write Conflict Detection"]
        WC1["Transaction T1 updates row R\n(marks R with T1.tx_id in VersionInfo)"]
        WC1 --> WC2["Transaction T2 tries\nto update same row R"]
        WC2 --> WC3{"R has uncommitted\nupdate from T1?"}
        WC3 -- Yes --> WC4["T2 detects write-write conflict"]
        WC4 --> WC5["Throw RuntimeError:\nwrite-write conflict, rollback T2"]
        WC3 -- No --> WC6["T2 proceeds with update"]
    end
```

## Planned Implementation
- `src/storage/column/version_info.cpp` — VersionInfo, delete/insert/update markers
- `src/transaction/undo_buffer.cpp` — UndoBuffer, version chain traversal
- `src/transaction/transaction_manager.cpp` — garbage collection of old versions
- `src/storage/table/row_group.cpp` — RowGroup MVCC filtering during scan
