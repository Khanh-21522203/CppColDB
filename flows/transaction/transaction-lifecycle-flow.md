# Transaction Lifecycle Flow

## Assumptions
- CppColDB supports auto-commit (implicit per-statement) and explicit BEGIN/COMMIT/ROLLBACK.
- TransactionManager assigns a monotonically increasing transaction_id and start_time to each transaction.
- Snapshot isolation: a transaction sees all data committed before its start_time.
- A single Transaction object tracks local changes for one session.

## Diagram

```mermaid
flowchart TD
    A["Client issues SQL"] --> B{"Explicit BEGIN\nor auto-commit?"}

    B -- "Explicit BEGIN" --> C["Connection sends BEGIN statement"]
    C --> D["TransactionManager::\nBeginTransaction()"]
    D --> E["Assign tx_id (monotonic counter)"]
    E --> F["Assign start_time (current commit counter)"]
    F --> G["Create Transaction object"]
    G --> H["Add to active_transactions list"]
    H --> I["Transaction Active\n(explicit mode: auto_commit = false)"]

    B -- "Auto-commit (default)" --> J["ClientContext::Query() begins"]
    J --> K["TransactionManager::\nBeginTransaction()"]
    K --> L["Same: assign tx_id + start_time"]
    L --> M["Transaction Active\n(auto_commit = true)"]

    I --> N["Execute Statement(s)"]
    M --> N

    N --> O{"Statement\nsucceeded?"}

    O -- "Yes, auto-commit" --> P["TransactionManager::Commit()"]
    P --> Q["Write WAL, apply changes,\nupdate version timestamps"]
    Q --> R["Remove from active_transactions"]
    R --> S["Transaction Committed"]

    O -- "Yes, explicit" --> T["Keep transaction open\nfor next statement\n(client sends more SQL)"]
    T --> N

    O -- "No, auto-commit" --> U["TransactionManager::Rollback()"]
    U --> V["Traverse UndoBuffer in reverse\nUndo all local changes"]
    V --> W["Remove from active_transactions"]
    W --> X["Transaction Rolled Back"]

    O -- "No, explicit" --> Y["Transaction stays open\nbut is now invalid"]
    Y --> Z["Client must issue ROLLBACK"]
    Z --> U

    T --> AA["Client issues COMMIT"]
    AA --> P

    T --> AB["Client issues ROLLBACK"]
    AB --> U

    subgraph SnapshotIsolation["Snapshot Isolation"]
        SI1["Transaction T with start_time = S"]
        SI1 --> SI2["T sees all rows committed\nwith commit_time < S"]
        SI2 --> SI3["T does NOT see rows committed\nafter T started"]
        SI3 --> SI4["T sees its own uncommitted changes\nvia UndoBuffer / TransactionLocalStorage"]
    end
```

## Planned Implementation
- `src/transaction/transaction_manager.cpp` — TransactionManager::BeginTransaction(), Commit(), Rollback()
- `src/transaction/transaction.cpp` — Transaction object, tx_id, start_time, UndoBuffer
- `src/main/client_context.cpp` — auto-commit management around Query()
