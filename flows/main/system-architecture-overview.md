# CppColDB System Architecture Overview

## Assumptions
- Single-node, single-process column-oriented database
- All subsystems communicate through well-defined interfaces
- Storage is either in-memory or single-file persistent

## Diagram

```mermaid
flowchart TD
    subgraph Client["Client Layer"]
        MAIN["main()"]
        REPL["SQL REPL\n(read-eval-print loop)"]
    end

    subgraph Connection["Connection Layer"]
        DB["Database\n(owns all subsystems)"]
        CONN["Connection\n(per-session handle)"]
        CTX["ClientContext\n(per-query state)"]
    end

    subgraph QueryEngine["Query Engine"]
        PARSER["Parser\n(SQL → AST)"]
        BINDER["Binder\n(AST → LogicalPlan)"]
        LPLANNER["LogicalPlanner\n(builds LogicalPlan)"]
        OPTIMIZER["Optimizer\n(predicate pushdown,\ncolumn pruning, join reorder)"]
        PPLANNER["PhysicalPlanner\n(LogicalPlan → PhysicalPlan)"]
    end

    subgraph Execution["Execution Engine"]
        EXEC["Executor\n(orchestrates pipelines)"]
        PIPE["Pipeline\n(Source → Operators → Sink)"]
        VECEXEC["VectorizedExecutor\n(DataChunk processing)"]
    end

    subgraph StorageLayer["Storage"]
        SM["StorageManager\n(coordinates storage)"]
        BM["BufferManager\n(memory pool, page cache)"]
        WAL["WAL\n(write-ahead log)"]
        CKPT["CheckpointManager\n(periodic flush to disk)"]
    end

    subgraph CatalogLayer["Catalog"]
        CAT["Catalog\n(schema registry)"]
        CE["CatalogEntry\n(tables, schemas, columns)"]
    end

    subgraph TxnLayer["Transaction"]
        TM["TransactionManager\n(assigns tx_id, tracks active txns)"]
        TX["Transaction\n(per-statement or explicit BEGIN/COMMIT)"]
        UB["UndoBuffer\n(change log for rollback/MVCC)"]
    end

    subgraph ColStorage["Column Storage"]
        RG["RowGroup\n(e.g. 122,880 rows)"]
        CC["ColumnChunk\n(one per column per RowGroup)"]
        CS["ColumnSegment\n(compressed block in BufferManager)"]
    end

    MAIN --> REPL
    REPL --> CONN
    CONN --> DB
    CONN --> CTX
    CTX --> PARSER
    PARSER --> BINDER
    BINDER --> CAT
    BINDER --> LPLANNER
    LPLANNER --> OPTIMIZER
    OPTIMIZER --> PPLANNER
    PPLANNER --> EXEC
    EXEC --> PIPE
    PIPE --> VECEXEC
    VECEXEC --> SM
    SM --> BM
    SM --> WAL
    SM --> CKPT
    SM --> RG
    RG --> CC
    CC --> CS
    CS --> BM
    CTX --> TM
    TM --> TX
    TX --> UB
    TX --> WAL
    CAT --> CE
```

## Key Design Principles
- Vectorized execution: operators process DataChunks (batches of ~1024 column vectors), not individual rows
- Column-oriented storage: data stored column-by-column within RowGroups for efficient scans and compression
- MVCC: each transaction sees a consistent snapshot; changes buffered in UndoBuffer until commit
- WAL-first writes: all persistent changes written to WAL before being applied to storage
- Buffer-managed I/O: all disk access goes through BufferManager for memory control

## Planned Implementation
- `src/main/database.cpp` — Database, Connection
- `src/main/client_context.cpp` — ClientContext
- `src/parser/` — Parser, Tokenizer, AST nodes (ParsedStatement)
- `src/planner/` — Binder, LogicalPlanner, PhysicalPlanner
- `src/optimizer/` — Optimizer passes
- `src/execution/` — Executor, Pipeline, VectorizedExecutor
- `src/storage/` — StorageManager, BufferManager, WAL, CheckpointManager
- `src/catalog/` — Catalog, CatalogEntry
- `src/transaction/` — TransactionManager, Transaction, UndoBuffer
- `src/storage/column/` — RowGroup, ColumnChunk, ColumnSegment
