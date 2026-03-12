# WAL Write Flow

## Assumptions
- WAL entries are written during commit, before changes are applied to the main storage.
- Each entry has the format: [entry_type: uint8][size: uint32][data: bytes].
- After all entries are written, the WAL is fsynced to guarantee durability.
- In-memory databases skip WAL writes entirely.

## Diagram

```mermaid
flowchart TD
    A["Transaction::WriteToWAL(wal)"] --> B{"In-memory\ndatabase?"}
    B -- Yes --> C["Skip WAL write\nReturn immediately"]
    B -- No --> D["Traverse UndoBuffer entries\nin commit order"]

    D --> E{"Entry type?"}

    E -- CATALOG_ENTRY --> F["WAL::WriteCatalogEntry(entry)"]
    F --> F1{"Catalog change\ntype?"}
    F1 -- CREATE TABLE --> F2["Serialize CREATE TABLE:\nschema, name, column defs, constraints"]
    F1 -- DROP TABLE --> F3["Serialize DROP TABLE:\nschema, name"]
    F1 -- CREATE SCHEMA --> F4["Serialize CREATE SCHEMA:\nname"]
    F1 -- DROP SCHEMA --> F5["Serialize DROP SCHEMA:\nname"]
    F2 --> G["Append to WAL buffer"]
    F3 --> G
    F4 --> G
    F5 --> G

    E -- INSERT_ROWS --> H["WAL::WriteInsert(table, chunk)"]
    H --> H1["Serialize: WAL_INSERT | size"]
    H1 --> H2["Serialize table identifier"]
    H2 --> H3["Serialize DataChunk\n(column-by-column)"]
    H3 --> G

    E -- DELETE_ROWS --> I["WAL::WriteDelete(table, row_ids)"]
    I --> I1["Serialize: WAL_DELETE | size"]
    I1 --> I2["Serialize table identifier"]
    I2 --> I3["Serialize deleted row_id list"]
    I3 --> G

    E -- UPDATE_ROWS --> J["WAL::WriteUpdate(table, cols, data)"]
    J --> J1["Serialize: WAL_UPDATE | size"]
    J1 --> J2["Serialize table + column list"]
    J2 --> J3["Serialize updated row_ids and new values"]
    J3 --> G

    G --> K{"More UndoBuffer\nentries?"}
    K -- Yes --> D
    K -- No --> L["All entries serialized"]

    L --> M["WAL::Flush()"]
    M --> N["Write WAL buffer to file\n(buffered I/O)"]
    N --> O["fsync(wal_fd)\n(guarantee durability on crash)"]
    O --> P["WAL write complete\nTransaction can proceed to commit"]

    C --> Q["Done"]
    P --> Q

    subgraph WALFormat["WAL Entry Format"]
        WF1["[entry_type: uint8]\n[data_size: uint32]\n[data: bytes]"]
        WF2["Entry types:\nWAL_CREATE_TABLE = 1\nWAL_DROP_TABLE = 2\nWAL_INSERT = 3\nWAL_DELETE = 4\nWAL_UPDATE = 5\nWAL_CREATE_SCHEMA = 6\nWAL_DROP_SCHEMA = 7\nWAL_CHECKPOINT = 8"]
    end
```

## Planned Implementation
- `src/storage/wal.cpp` — WAL::WriteInsert(), WriteDelete(), WriteUpdate(), WriteCatalogEntry(), Flush()
- `src/transaction/transaction.cpp` — Transaction::WriteToWAL()
