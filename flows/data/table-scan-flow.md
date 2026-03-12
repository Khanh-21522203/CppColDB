# Table Scan Flow (Column-Oriented)

## Assumptions
- Table scans are the primary Source operator in query pipelines.
- Data is stored column-by-column in RowGroups; only the requested columns are read (column pruning).
- Each ColumnChunk is loaded from the BufferManager and decompressed into a DataVector.
- MVCC filtering is applied per-row after assembling the DataChunk.
- Zone maps (min/max statistics per ColumnChunk) allow entire RowGroups to be skipped.

## Diagram

```mermaid
flowchart TD
    A["PhysicalTableScan::GetData(scan_state, chunk)"] --> B["Get next RowGroup\nfrom scan_state.current_row_group"]
    B --> C{"RowGroup\navailable?"}
    C -- No --> D["Scan complete:\nreturn FINISHED"]

    C -- Yes --> E{"Zone map check:\ncan this RowGroup\nsatisfy the filter?"}
    E -- No --> F["Skip RowGroup\n(advance to next)"]
    F --> B

    E -- Yes --> G["For each column_id\nin projection list"]
    G --> H["ColumnChunk::Scan(row_group, col_id, scan_state)"]
    H --> I["BufferManager::Pin(block_id)\nLoad ColumnSegment block into memory"]
    I --> J["Decompress block\ninto DataVector\n(detect compression from segment header)"]
    J --> K["Fill DataChunk column slot\nwith DataVector data"]
    K --> L{"More columns\nto scan?"}
    L -- Yes --> G
    L -- No --> M["DataChunk assembled\n(all requested columns)"]

    M --> N["Apply MVCC filter\nover each row in chunk"]
    N --> O{"Row visibility\ncheck (per row)"}
    O -- "No VersionInfo:\nunmodified committed row" --> P["Row visible: keep"]
    O -- "Delete committed\nbefore tx.start_time" --> Q["Row invisible: mark null\nor compact out"]
    O -- "Update present" --> R["Fetch correct version\nfrom UndoBuffer chain"]
    O -- "T's own uncommitted\ndelete" --> Q
    R --> P

    P --> S{"All rows\nchecked?"}
    Q --> S
    S -- No --> O
    S -- Yes --> T["Apply pushed-down filter predicates\nover DataChunk columns"]

    T --> U{"Chunk has\nany rows?"}
    U -- No --> B
    U -- Yes --> V["Return DataChunk\nto pipeline (HAVE_MORE_OUTPUT)"]

    subgraph ColumnStorage["Column Storage Structure"]
        CS1["Table\n  └─ RowGroup 0  (rows 0–122879)\n       ├─ ColumnChunk[col_0]\n       │    └─ ColumnSegment (block_id=42)\n       └─ ColumnChunk[col_1]\n            └─ ColumnSegment (block_id=43)"]
        CS2["RowGroup 1  (rows 122880–245759)\n  └─ ..."]
    end
```

## Planned Implementation
- `src/execution/operator/physical_table_scan.cpp` — PhysicalTableScan::GetData()
- `src/storage/column/row_group.cpp` — RowGroup scan coordination
- `src/storage/column/column_chunk.cpp` — ColumnChunk::Scan()
- `src/storage/buffer_manager.cpp` — BufferManager::Pin()
- `src/storage/column/compression.cpp` — Decompress into DataVector
