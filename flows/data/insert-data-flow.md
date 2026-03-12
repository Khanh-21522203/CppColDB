# Insert Data Flow

## Assumptions
- INSERT goes through the full query pipeline: parse → bind → optimize → physical plan → execute.
- During execution, new rows are written to TransactionLocalStorage (column vectors private to the transaction).
- On commit, the local storage is merged into the main RowGroup storage, making rows visible to others.
- The WAL entry is written during commit, before the merge.

## Diagram

```mermaid
flowchart TD
    A["INSERT INTO table VALUES (...)"] --> B["Parse → Bind → Optimize"]
    B --> C["PhysicalInsert operator\nin pipeline Sink"]

    C --> D["Pipeline Source:\ngenerate insert data\n(from VALUES list or sub-query)"]
    D --> E["PhysicalInsert::Consume(chunk)"]
    E --> F["TransactionLocalStorage::Append(table, chunk)"]

    F --> G{"Local storage\nexists for table?"}
    G -- No --> H["Create TransactionLocalStorage\n(column vectors for this table)"]
    G -- Yes --> I["Use existing local storage"]
    H --> I

    I --> J["Append chunk columns\ninto local column vectors"]
    J --> K{"Table has\nUNIQUE/PRIMARY KEY constraints?"}
    K -- Yes --> L["Check constraint against\nexisting rows + local storage"]
    L --> M{"Constraint\nviolation?"}
    M -- Yes --> N["Throw RuntimeError:\nconstraint violation"]
    M -- No --> O["Local append recorded"]
    K -- No --> O

    O --> P{"More chunks\nto insert?"}
    P -- Yes --> D
    P -- No --> Q["INSERT execution complete\n(rows in local storage only)"]

    Q --> R["--- ON COMMIT ---"]
    R --> S["Transaction::WriteToWAL()\nSerialize INSERT entries to WAL\n(see wal-write-flow.md)"]
    S --> T["WAL::Flush() + fsync"]
    T --> U["Merge TransactionLocalStorage\ninto main RowGroup storage"]

    U --> V{"Existing RowGroup\nhas space?"}
    V -- Yes --> W["Append rows to\ncurrent RowGroup"]
    V -- No --> X["Create new RowGroup\nAppend rows there"]
    W --> Y["RowGroup::CommitAppend(commit_time)\nRows become visible to other transactions"]
    X --> Y

    Y --> Z["Free TransactionLocalStorage\nfor this table"]
```

## Planned Implementation
- `src/execution/operator/physical_insert.cpp` — PhysicalInsert::Consume()
- `src/transaction/transaction_local_storage.cpp` — TransactionLocalStorage::Append()
- `src/transaction/transaction_manager.cpp` — Commit() → merge local storage
- `src/storage/column/row_group.cpp` — RowGroup::Append(), CommitAppend()
- `src/storage/wal.cpp` — WAL insert serialization
