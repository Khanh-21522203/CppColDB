# CppColDB — Architecture Diagrams

Mermaid diagrams covering system architecture, query execution, storage internals, transactions, and pipeline operator flow.

---

## 1. System Architecture (C4 Container)

High-level view of deployable units and their interactions.

```mermaid
flowchart TB
    User(["👤 User / Application"])

    subgraph db["CppColDB Engine"]
        direction TB

        subgraph entry["Entry Layer"]
            CLI["main.cpp\nCLI REPL"]
            CONN["Connection\nSession handle"]
            CTX["ClientContext\nPer-query context"]
        end

        subgraph pipeline["Query Pipeline"]
            TOK["Tokenizer"]
            PAR["Parser\nAST builder"]
            BIND["Binder\nSemantic analysis"]
            OPT["Optimizer\nLogical rewrites"]
            PHYS["PhysicalPlanner\nOperator tree"]
            EXEC["Executor\nPipeline builder"]
            PIPE["PipelineExecutor\nRuntime"]
        end

        subgraph storage["Storage Layer"]
            CAT["Catalog\nSchema / DDL"]
            RG["RowGroup\nColumnar data"]
            CC["ColumnChunk\nPer-column data"]
            BUF["BufferManager\nLRU block cache"]
            BLK["BlockFile\nDisk I/O"]
        end

        subgraph txn["Transaction Layer"]
            TXM["TransactionManager\nACID lifecycle"]
            TX["Transaction\nUndo buffer"]
            VI["VersionInfo\nMVCC row markers"]
            WAL["WAL\nWrite-ahead log"]
            CHK["CheckpointManager\nDurability"]
        end

        PROF["QueryProfiler\nTiming / EXPLAIN"]
        SCHED["TaskScheduler\nBackground threads"]
    end

    User -->|"SQL string"| CLI
    CLI --> CONN
    CONN --> CTX
    CTX --> TOK --> PAR --> BIND --> OPT --> PHYS --> EXEC --> PIPE
    BIND -->|"resolve names"| CAT
    PIPE -->|"scan rows"| RG
    PIPE -->|"DML"| RG
    PIPE -->|"DDL"| CAT
    RG --> CC --> BUF --> BLK
    CONN --> TXM
    TXM --> TX
    TX --> VI
    TX --> WAL
    CHK --> WAL
    CHK --> BLK
    SCHED -->|"async checkpoint"| CHK
    EXEC --> PROF
```

---

## 2. Query Execution Flow (Sequence)

Full lifecycle from SQL string to result rows.

```mermaid
sequenceDiagram
    participant U as User
    participant CO as Connection
    participant TM as TransactionManager
    participant CC as ClientContext
    participant PA as Parser
    participant BI as Binder
    participant OP as Optimizer
    participant PP as PhysicalPlanner
    participant EX as Executor
    participant PE as PipelineExecutor
    participant SC as TableScan
    participant RG as RowGroup

    U->>CO: Query(sql)
    CO->>TM: BeginTransaction()
    TM-->>CO: Transaction (snapshot_time)

    CO->>CC: Query(sql, txn)
    CC->>PA: Tokenize + Parse(sql)
    PA-->>CC: ParsedStatement AST

    CC->>BI: Bind(ast, catalog, txn)
    BI-->>CC: LogicalPlan

    CC->>OP: Optimize(logical_plan)
    OP-->>CC: Optimized LogicalPlan
    Note over OP: predicate pushdown,<br/>column pruning,<br/>constant folding

    CC->>PP: Plan(logical_plan)
    PP-->>CC: PhysicalOperator tree

    CC->>EX: Execute(plan)
    EX->>EX: BuildPipelines()
    Note over EX: topological sort<br/>of pipeline DAG

    loop For each Pipeline (in dependency order)
        EX->>PE: Execute(pipeline)

        loop While data available
            PE->>SC: GetData(chunk)
            SC->>RG: Scan(snapshot_time)
            RG-->>SC: DataChunk (filtered by MVCC)
            SC-->>PE: DataChunk

            PE->>PE: Apply operators<br/>(Filter, Projection, etc.)
            PE->>PE: Sink.Consume(chunk)
        end
    end

    EX-->>CC: QueryResult
    CC-->>CO: QueryResult

    CO->>TM: Commit()
    TM->>TM: WriteToWAL()
    TM->>TM: ApplyUndoBuffer()
    TM-->>CO: done

    CO-->>U: QueryResult{rows, columns}
```

---

## 3. Pipeline Operator Roles

How operators are organized into pipelines for two-phase operations.

```mermaid
flowchart LR
    subgraph p1["Pipeline 1 — Build Phase"]
        direction LR
        SRC1["TableScan\n(SOURCE)"] --> F1["Filter\n(OPERATOR)"] --> SINK1["HashJoinBuild\n(SINK)"]
    end

    subgraph p2["Pipeline 2 — Probe Phase"]
        direction LR
        SRC2["TableScan\n(SOURCE)"] --> F2["Filter\n(OPERATOR)"] --> PROBE["HashJoinProbe\n(OPERATOR)"] --> PROJ["Projection\n(OPERATOR)"] --> COLL["Collector\n(SINK)"]
    end

    subgraph p3["Pipeline 3 — Sort"]
        direction LR
        SRC3["TableScan\n(SOURCE)"] --> SORTSNK["PhysicalSort\n(SINK)"]
    end

    subgraph p4["Pipeline 4 — Sort Output"]
        direction LR
        SORTSRC["PhysicalSortSource\n(SOURCE)"] --> LMIT["Limit\n(OPERATOR)"] --> COLL2["Collector\n(SINK)"]
    end

    subgraph p5["Pipeline 5 — Aggregation"]
        direction LR
        SRC5["TableScan\n(SOURCE)"] --> AGGSNK["HashAggregation\n(SINK)"]
    end

    subgraph p6["Pipeline 6 — Agg Output"]
        direction LR
        AGGSRC["AggregationSource\n(SOURCE)"] --> PROJ2["Projection\n(OPERATOR)"] --> COLL3["Collector\n(SINK)"]
    end

    p1 -->|"JoinHashTable"| p2
    p3 -->|"SortBuffer"| p4
    p5 -->|"AggHashTable"| p6
```

---

## 4. Storage Layer Internals

Column-oriented storage with compression and buffer management.

```mermaid
flowchart TB
    subgraph table["Table (CatalogEntry)"]
        direction TB

        subgraph rg0["RowGroup 0 (up to 120K rows)"]
            direction LR
            subgraph col0["ColumnChunk (col 0)"]
                PD0["pending_data_\nDataVector (in-memory)"]
                SEG01["Segment 0\nRLE compressed\nblk_id=5"]
                SEG02["Segment 1\nDelta compressed\nblk_id=7"]
            end
            subgraph col1["ColumnChunk (col 1)"]
                PD1["pending_data_\nDataVector"]
                SEG11["Segment 0\nDictionary\nblk_id=8"]
            end
            VI0["VersionInfo\nMVCC markers\n(INSERT/DELETE/UPDATE)"]
        end

        subgraph rg1["RowGroup 1"]
            DOTS["..."]
        end
    end

    subgraph bm["BufferManager (LRU, 256 MiB)"]
        F0["Frame blk_id=5\n[pinned]"]
        F1["Frame blk_id=7"]
        F2["Frame blk_id=8"]
        LRU["LRU Eviction\nwhen pool full"]
    end

    subgraph bf["BlockFile (disk)"]
        B0["Block 0\nCatalog metadata"]
        B5["Block 5"]
        B7["Block 7"]
        B8["Block 8"]
    end

    SEG01 -->|"pin(5)"| F0
    SEG02 -->|"pin(7)"| F1
    SEG11 -->|"pin(8)"| F2

    F0 <-->|"read/write"| B5
    F1 <-->|"read/write"| B7
    F2 <-->|"read/write"| B8

    style PD0 fill:#d4edda
    style PD1 fill:#d4edda
    style VI0 fill:#fff3cd
```

---

## 5. MVCC Transaction Lifecycle (State)

Transaction states and visibility rules.

```mermaid
stateDiagram-v2
    [*] --> Active : BeginTransaction()

    Active --> Committing : Commit()
    Active --> RolledBack : Rollback()

    Committing --> WALWritten : WriteToWAL()
    WALWritten --> Applied : ApplyUndoBuffer()
    Applied --> Committed : set commit_time on markers

    RolledBack --> Reversed : ReverseUndoBuffer()
    Reversed --> [*]
    Committed --> [*]

    note right of Active
        Rows inserted: marked INSERT (tx_id, commit_time=INVALID)
        Rows deleted: marked DELETE (tx_id, commit_time=INVALID)
        Rows updated: old values saved in UndoBuffer
    end note

    note right of Committed
        All markers get commit_time = commit_counter
        Other txns with snapshot > commit_time see changes
    end note
```

---

## 6. MVCC Row Visibility

How snapshot isolation decides which rows a transaction sees.

```mermaid
flowchart TD
    START["RowGroup::Scan(snapshot_time)"]
    FAST["AllInsertedVisibleTo(snapshot_time)?"]
    FASTYES["Return all rows\n(O(1) fast path)"]
    PERROW["Per-row: check VersionMarker"]

    CHECK_INSERT["marker.type == INSERT?"]
    CHECK_DELETE["marker.type == DELETE?"]
    CHECK_UPDATE["marker.type == UPDATE?"]

    VISIBLE_INSERT["marker.commit_time <= snapshot_time\n→ VISIBLE"]
    HIDDEN_INSERT["marker.commit_time > snapshot_time\n OR commit_time == INVALID\n→ HIDDEN"]

    HIDDEN_DELETE["marker.commit_time <= snapshot_time\n→ HIDDEN (deleted)"]
    VISIBLE_DELETE["commit_time > snapshot_time\n OR INVALID → VISIBLE\n(delete not yet committed)"]

    VISIBLE_UPDATE["fetch old values from UndoBuffer\n(see pre-update state)"]

    START --> FAST
    FAST -->|"yes"| FASTYES
    FAST -->|"no"| PERROW
    PERROW --> CHECK_INSERT
    CHECK_INSERT -->|"yes"| VISIBLE_INSERT
    CHECK_INSERT -->|"no"| CHECK_DELETE
    VISIBLE_INSERT --> HIDDEN_INSERT
    CHECK_DELETE -->|"yes"| HIDDEN_DELETE
    CHECK_DELETE -->|"no"| CHECK_UPDATE
    HIDDEN_DELETE --> VISIBLE_DELETE
    CHECK_UPDATE -->|"yes"| VISIBLE_UPDATE
```

---

## 7. WAL & Recovery Flow

Durability path from commit to crash recovery.

```mermaid
sequenceDiagram
    participant TX as Transaction
    participant WAL as WAL
    participant BM as BufferManager
    participant CHK as CheckpointManager
    participant BLK as BlockFile
    participant DB as Database (startup)

    Note over TX,WAL: --- Normal Commit ---
    TX->>WAL: Write(INSERT/DELETE/UPDATE entries)
    WAL->>WAL: Flush() / fsync
    TX->>TX: ApplyUndoBuffer() (in-memory)

    Note over CHK,BLK: --- Checkpoint ---
    CHK->>WAL: WriteCheckpointMarker()
    CHK->>BM: FlushAllRowGroups()
    CHK->>BLK: Serialize catalog → Block 0
    CHK->>BM: Flush() (all dirty blocks)
    CHK->>WAL: Truncate() (keep post-checkpoint tail)

    Note over DB,WAL: --- Crash Recovery ---
    DB->>BLK: Deserialize catalog (Block 0)
    DB->>WAL: Open for replay
    WAL-->>DB: Scan to last CHECKPOINT marker
    loop Entries after checkpoint
        WAL-->>DB: WAL entry (INSERT/DELETE/UPDATE/DDL)
        DB->>DB: Replay entry (re-apply to RowGroups/Catalog)
    end
    DB->>WAL: Re-open for write
```

---

## 8. Partitioning — Routing & Pruning

How rows are routed on insert and partitions are pruned on scan.

```mermaid
flowchart LR
    subgraph insert["INSERT path"]
        ROW["Input Row"]
        ROUTE["PartitionInfo::RouteRow()\nEvaluate partition key"]

        subgraph parts["Partition-Specific RowGroups"]
            P0["Partition 0\nRowGroup(s)"]
            P1["Partition 1\nRowGroup(s)"]
            P2["Partition 2\nRowGroup(s)"]
        end

        ROW --> ROUTE
        ROUTE -->|"RANGE: key < 100"| P0
        ROUTE -->|"RANGE: 100 ≤ key < 200"| P1
        ROUTE -->|"RANGE: key ≥ 200"| P2
    end

    subgraph scan["SCAN path (with WHERE key = 150)"]
        PRED["Predicate Analysis"]
        PRUNE["PartitionInfo::PrunedPartitions()"]
        SKIP["Skip Partition 0 ✗\nSkip Partition 2 ✗"]
        READ["Read Partition 1 only ✓"]

        PRED --> PRUNE --> SKIP --> READ
    end
```

---

## 9. Logical → Physical Plan Mapping

How logical plan nodes become physical operator trees.

```mermaid
flowchart LR
    subgraph logical["Logical Plan (after Optimizer)"]
        direction TB
        LJ["LogicalJoin\n(INNER, col_a = col_b)"]
        LF1["LogicalFilter\n(age > 25)"]
        LG1["LogicalGet\n(table: orders)"]
        LA["LogicalAggregate\n(GROUP BY dept, SUM salary)"]
        LF2["LogicalFilter\n(active = true)"]
        LG2["LogicalGet\n(table: employees)"]

        LJ --> LF1 --> LG1
        LJ --> LA --> LF2 --> LG2
    end

    subgraph physical["Physical Plan"]
        direction TB
        PJP["PhysicalHashJoinProbe"]
        PJB["PhysicalHashJoinBuild"]
        PF1["PhysicalFilter"]
        PTS1["PhysicalTableScan\n(orders)"]
        PAS["PhysicalAggregationSource"]
        PHA["PhysicalHashAggregation"]
        PF2["PhysicalFilter"]
        PTS2["PhysicalTableScan\n(employees)"]

        PJP --> PF1 --> PTS1
        PJP -->|"probe"| PJB
        PJB --> PAS --> PHA --> PF2 --> PTS2
    end

    LJ -.->|"maps to"| PJP
    LF1 -.->|"maps to"| PF1
    LG1 -.->|"maps to"| PTS1
    LA -.->|"maps to"| PHA
    LF2 -.->|"maps to"| PF2
    LG2 -.->|"maps to"| PTS2
```

---

## 10. Class Hierarchy — Physical Operators

```mermaid
classDiagram
    class PhysicalOperator {
        <<abstract>>
        +OperatorRole role
        +GetData(chunk) OperatorResult
        +Execute(input, output) OperatorResult
        +Consume(chunk) OperatorResult
        +TryFlush(chunk) OperatorResult
    }

    class PhysicalTableScan {
        -TableCatalogEntry* table
        -Transaction* txn
        -PartitionInfo* partition_info
        +GetData(chunk) OperatorResult
    }

    class PhysicalFilter {
        -LogicalExpr* predicate
        +Execute(input, output) OperatorResult
    }

    class PhysicalProjection {
        -vector~LogicalExpr*~ exprs
        +Execute(input, output) OperatorResult
    }

    class PhysicalHashAggregation {
        -AggregateHashTable hash_table
        +Consume(chunk) OperatorResult
        +TryFlush(chunk) OperatorResult
    }

    class PhysicalAggregationSource {
        -AggregateHashTable* hash_table
        +GetData(chunk) OperatorResult
    }

    class PhysicalHashJoinBuild {
        -JoinHashTable hash_table
        +Consume(chunk) OperatorResult
    }

    class PhysicalHashJoinProbe {
        -JoinHashTable* build_table
        +Execute(input, output) OperatorResult
    }

    class PhysicalSort {
        -SortBuffer sort_buffer
        +Consume(chunk) OperatorResult
    }

    class PhysicalSortSource {
        -SortBuffer* sort_buffer
        +GetData(chunk) OperatorResult
    }

    class PhysicalInsert {
        -TableCatalogEntry* table
        -Transaction* txn
        +GetData(chunk) OperatorResult
    }

    PhysicalOperator <|-- PhysicalTableScan
    PhysicalOperator <|-- PhysicalFilter
    PhysicalOperator <|-- PhysicalProjection
    PhysicalOperator <|-- PhysicalHashAggregation
    PhysicalOperator <|-- PhysicalAggregationSource
    PhysicalOperator <|-- PhysicalHashJoinBuild
    PhysicalOperator <|-- PhysicalHashJoinProbe
    PhysicalOperator <|-- PhysicalSort
    PhysicalOperator <|-- PhysicalSortSource
    PhysicalOperator <|-- PhysicalInsert

    PhysicalHashAggregation "1" ..> "1" PhysicalAggregationSource : materializes into
    PhysicalHashJoinBuild "1" ..> "1" PhysicalHashJoinProbe : feeds
    PhysicalSort "1" ..> "1" PhysicalSortSource : materializes into
```

---

## 11. Data Model — Storage Entities (ER)

```mermaid
erDiagram
    Database ||--|{ Connection : "creates"
    Database ||--|| Catalog : "owns"
    Database ||--|| TransactionManager : "owns"
    Database ||--|| BufferManager : "owns"
    Database ||--|| WAL : "owns"

    Catalog ||--|{ Schema : "contains"
    Schema ||--|{ TableCatalogEntry : "contains"

    TableCatalogEntry ||--|{ ColumnDefinition : "has"
    TableCatalogEntry ||--|{ RowGroup : "stores data in"
    TableCatalogEntry ||--o| PartitionInfo : "partitioned by"

    RowGroup ||--|{ ColumnChunk : "has one per column"
    RowGroup ||--|| VersionInfo : "tracks MVCC via"

    ColumnChunk ||--o{ ColumnSegment : "compressed into"
    ColumnSegment ||--o| BufferFrame : "cached in"
    BufferFrame ||--|| BlockFile : "persisted to"

    Transaction ||--|| UndoBuffer : "accumulates"
    UndoBuffer ||--|{ UndoEntry : "contains"
    VersionInfo ||--|{ VersionMarker : "holds"
    VersionMarker }o--|| Transaction : "belongs to"

    Transaction {
        uint64 tx_id
        uint64 start_time
        uint64 commit_time
    }
    VersionMarker {
        enum type
        uint64 tx_id
        uint64 commit_time
    }
    RowGroup {
        int max_rows
        int row_count
    }
    ColumnChunk {
        TypeId type
        int num_rows
    }
```
