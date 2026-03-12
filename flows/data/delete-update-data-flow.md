# Delete & Update Data Flow

## Assumptions
- DELETE and UPDATE use MVCC version markers rather than in-place modification.
- Deletes write a delete marker into VersionInfo; updates save old values in UndoBuffer.
- Other transactions reading during an uncommitted delete/update see the original data.
- On commit, delete markers and update version timestamps become permanent.

## Diagram

```mermaid
flowchart TD
    A["DELETE FROM table WHERE condition"] --> B["Parse → Bind → Optimize"]
    B --> C["Pipeline: PhysicalTableScan → Filter → PhysicalDelete"]

    C --> D["Scan table rows\nmatching WHERE predicate"]
    D --> E["PhysicalDelete::Consume(row_ids)"]
    E --> F["For each row_id in chunk"]
    F --> G["VersionInfo::MarkDeleted(row_id, tx.tx_id)"]
    G --> H["Write DeleteEntry\nto UndoBuffer\n(stores row_id for rollback)"]
    H --> I{"More rows\nto delete?"}
    I -- Yes --> F
    I -- No --> J["Delete recorded in transaction\n(rows still visible to other readers)"]

    J --> K["--- ON COMMIT ---"]
    K --> L["Traverse UndoBuffer: DELETE entries"]
    L --> M["VersionInfo::CommitDelete(row_id, commit_time)\nRows become invisible to\ntransactions starting after commit_time"]

    subgraph UpdateFlow["UPDATE Path"]
        U1["UPDATE table SET col = expr WHERE condition"]
        U1 --> U2["Parse → Bind → Optimize"]
        U2 --> U3["Pipeline: Scan → Filter → PhysicalUpdate"]
        U3 --> U4["Scan matching rows\nread current column values"]
        U4 --> U5["Evaluate SET expressions\ncompute new column values"]
        U5 --> U6["For each matched row"]
        U6 --> U7["Save old values in UndoBuffer\n(UpdateEntry with old_values + row_id)"]
        U7 --> U8["Write new values to\nTransactionLocalStorage (delta buffer)"]
        U8 --> U9["VersionInfo::MarkUpdated(row_id, tx.tx_id)"]
        U9 --> U10{"More rows?"}
        U10 -- Yes --> U6
        U10 -- No --> U11["Update recorded in transaction\n(other readers see old values via UndoBuffer)"]
        U11 --> U12["--- ON COMMIT ---"]
        U12 --> U13["Apply delta buffer\nto main ColumnChunk storage"]
        U13 --> U14["VersionInfo::CommitUpdate(row_id, commit_time)\nNew values visible to\ntransactions starting after commit_time"]
    end

    subgraph MVCCRead["MVCC Read Visibility During Uncommitted Changes"]
        MR1["Reader T with start_time = S"]
        MR1 --> MR2{"VersionInfo\nfor row?"}
        MR2 -- "Delete marker, tx_id != T, uncommitted" --> MR3["Row still visible to T"]
        MR2 -- "Update marker, tx_id != T, uncommitted" --> MR4["T reads original values\nfrom UndoBuffer chain"]
        MR2 -- "Committed before S" --> MR5["Normal visibility rules apply"]
    end
```

## Planned Implementation
- `src/execution/operator/physical_delete.cpp` — PhysicalDelete::Consume()
- `src/execution/operator/physical_update.cpp` — PhysicalUpdate::Consume()
- `src/storage/column/version_info.cpp` — MarkDeleted(), CommitDelete(), MarkUpdated(), CommitUpdate()
- `src/transaction/undo_buffer.cpp` — DeleteEntry, UpdateEntry storage
- `src/transaction/transaction_manager.cpp` — Commit() delete/update application
