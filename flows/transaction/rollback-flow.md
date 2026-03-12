# Transaction Rollback Flow

## Assumptions
- Rollback reverses all local changes using the UndoBuffer, traversed in reverse order.
- Rollback can be triggered explicitly (ROLLBACK statement), on auto-commit error, or on Connection close.
- No WAL entry is written for a rollback; uncommitted changes were never written to WAL.

## Diagram

```mermaid
flowchart TD
    A["Rollback triggered"] --> B{"Trigger source?"}

    B -- "Explicit ROLLBACK" --> C["Connection sends ROLLBACK\nto ClientContext"]
    B -- "Auto-commit error" --> D["ClientContext::Query()\nexecution failed"]
    B -- "Connection closed\nwith open transaction" --> E["Connection destructor"]

    C --> F["TransactionManager::Rollback(transaction)"]
    D --> F
    E --> F

    F --> G["Traverse UndoBuffer\nin reverse order (LIFO)"]
    G --> H{"Entry type?"}

    H -- CATALOG_ENTRY --> I["Restore previous CatalogEntry:\nremove from Catalog or\nrestore prior version"]
    H -- INSERT_ROWS --> J["RowGroup::RevertAppend()\nRemove rows appended by this transaction\nfrom RowGroup"]
    H -- DELETE_ROWS --> K["VersionInfo::ClearDeleteMarker()\nRestore delete-marked rows to visible"]
    H -- UPDATE_ROWS --> L["VersionInfo::RevertUpdate()\nRestore original column values\nfrom UndoBuffer entry"]

    I --> M["Next entry (reverse)"]
    J --> M
    K --> M
    L --> M
    M --> G

    G --> N{"All UndoBuffer\nentries reversed?"}
    N -- Yes --> O["Discard TransactionLocalStorage\n(any un-merged inserts)"]
    O --> P["Remove transaction\nfrom active_transactions"]
    P --> Q["Free UndoBuffer memory"]
    Q --> R["Rollback complete"]

    subgraph NoWALEntry["Note: No WAL Write on Rollback"]
        NW1["Uncommitted changes were never\nwritten to the WAL"]
        NW1 --> NW2["On crash, the WAL contains only\ncommitted transactions"]
        NW2 --> NW3["Rolled-back changes disappear\nnaturally — nothing to undo at replay"]
    end
```

## Planned Implementation
- `src/transaction/transaction_manager.cpp` — TransactionManager::Rollback()
- `src/transaction/transaction.cpp` — UndoBuffer traversal
- `src/storage/column/row_group.cpp` — RowGroup::RevertAppend()
- `src/storage/column/version_info.cpp` — VersionInfo::ClearDeleteMarker(), RevertUpdate()
