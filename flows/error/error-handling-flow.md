# Error Handling Flow

## Assumptions
- CppColDB uses C++ exceptions for error propagation; no error code threading.
- Errors are classified by type: ParseError, BindError, RuntimeError, IOError.
- On any unhandled error during query execution, the current transaction is rolled back.
- Auto-commit queries rollback automatically; explicit transactions require a client ROLLBACK.

## Diagram

```mermaid
flowchart TD
    A["Exception thrown during\nparse / bind / optimize / execute"] --> B{"Exception type?"}

    B -- ParseError --> C["SQL syntax was invalid\ninvalidates_query = true\ntransaction stays valid"]
    B -- BindError --> D["Unknown table/column/type\ninvalidates_query = true\ntransaction stays valid"]
    B -- RuntimeError --> E["Execution failure\n(division by zero, constraint violation, etc.)\ninvalidates_query = true\ntransaction invalid for auto-commit"]
    B -- IOError --> F["Disk / storage failure\ninvalidates_query = true\ntransaction invalid, database may need restart"]

    C --> G["Catch in ClientContext::Query()"]
    D --> G
    E --> G
    F --> G

    G --> H["Build error result:\nerror message + error type"]
    H --> I{"Auto-commit\ntransaction active?"}
    I -- Yes --> J["TransactionManager::Rollback()\nUndo all local changes"]
    J --> K["Return QueryResult with error"]
    I -- No --> L{"Error type\nallows retry?"}
    L -- "ParseError or BindError" --> M["Transaction stays open\nClient can issue another statement"]
    L -- "RuntimeError or IOError" --> N["Transaction is invalid\nClient must issue ROLLBACK"]

    K --> O["Error returned to caller"]
    M --> O
    N --> O

    subgraph ErrorTypes["Error Type Reference"]
        ET1["ParseError\n  — Tokenize failure\n  — Unexpected token\n  — Unterminated string"]
        ET2["BindError\n  — Table not found\n  — Column not found\n  — Type mismatch in expression"]
        ET3["RuntimeError\n  — Division by zero\n  — Constraint violation\n  — Out of memory\n  — Type cast failure"]
        ET4["IOError\n  — Cannot read block from disk\n  — WAL write failure\n  — BlockFile corruption"]
    end

    subgraph ExceptionHierarchy["Exception Class Hierarchy"]
        EH1["CppColDBException (base)"]
        EH1 --> EH2["ParseError"]
        EH1 --> EH3["BindError"]
        EH1 --> EH4["RuntimeError"]
        EH1 --> EH5["IOError"]
    end
```

## Planned Implementation
- `src/common/exception.cpp` — CppColDBException, ParseError, BindError, RuntimeError, IOError
- `src/main/client_context.cpp` — top-level catch in Query()
- `src/transaction/transaction_manager.cpp` — Rollback() on error path
