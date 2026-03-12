# Catalog Lookup Flow

## Assumptions
- The Catalog is organized as: Catalog → Schema → CatalogEntry (table, view, column).
- Lookups are MVCC-aware: only entries visible to the current transaction are returned.
- An entry is visible if its commit_time is before the transaction's start_time, or it was created by the same transaction.
- A deleted entry (marked with a delete_time) is invisible if the delete was committed before the transaction started.

## Diagram

```mermaid
flowchart TD
    A["Catalog::GetEntry(schema_name, entry_name, tx)"] --> B{"Schema name\nspecified?"}
    B -- No --> C["Default to 'main' schema"]
    B -- Yes --> D["Look up schema by name"]
    C --> E["Catalog::GetSchema(schema_name)"]
    D --> E

    E --> F{"Schema\nfound?"}
    F -- No --> G["Throw BindError:\nschema not found"]
    F -- Yes --> H["Schema::GetEntry(entry_name)"]

    H --> I{"Entry exists\nin schema?"}
    I -- No --> J{"on_not_found\npolicy?"}
    J -- RETURN_NULL --> K["Return nullptr"]
    J -- THROW --> L["Throw BindError:\ntable/view not found"]

    I -- Yes --> M["MVCC Visibility Check"]

    M --> N{"entry.create_time\n== tx.tx_id?"}
    N -- Yes --> O["Created by this transaction\n→ visible"]
    N -- No --> P{"entry.create_commit_time\n< tx.start_time?"}
    P -- No --> Q["Entry created after T started\n→ not visible"]
    Q --> J
    P -- Yes --> R["Entry was committed\nbefore T started"]

    R --> S{"entry.delete_commit_time\n== 0?"}
    S -- Yes --> T["Not deleted\n→ visible, return entry"]
    S -- No --> U{"entry.delete_commit_time\n< tx.start_time?"}
    U -- Yes --> V["Deleted before T started\n→ not visible"]
    V --> J
    U -- No --> W["Delete committed after T started\nor uncommitted\n→ entry still visible to T"]

    O --> X["Return CatalogEntry"]
    T --> X
    W --> X
```

## Planned Implementation
- `src/catalog/catalog.cpp` — Catalog::GetEntry(), GetSchema()
- `src/catalog/schema.cpp` — Schema::GetEntry()
- `src/catalog/catalog_entry.cpp` — CatalogEntry with create/delete timestamps
- `src/transaction/transaction.cpp` — Transaction::IsVisible(entry)
