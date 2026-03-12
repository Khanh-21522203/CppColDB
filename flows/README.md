# CppColDB Architecture Diagrams

This directory contains Mermaid diagrams documenting the CppColDB system architecture. Each file contains exactly one Mermaid diagram. Diagrams describe the planned architecture for a column-oriented relational database built from scratch in C++.

## Directory Structure

```
flows/
├── main/                  — High-level architecture overview
├── bootstrap/             — Database initialization and shutdown
├── query-processing/      — Parsing, planning, optimization, execution
├── parallel/              — Pipeline execution, task scheduling
├── storage/               — Buffer management, WAL, checkpointing, column storage
├── transaction/           — Commit, rollback, MVCC, lifecycle
├── data/                  — Catalog, DML operations, table scans
├── execution/             — Vectorized execution, joins, aggregation
├── error/                 — Error handling and classification
└── monitoring/            — Query profiling
```

---

## main/ — Architecture Overview

| File | Type | Description |
|------|------|-------------|
| [system-architecture-overview.md](main/system-architecture-overview.md) | Flowchart | High-level component architecture: client, connection, query engine, execution, storage, catalog, transaction, column storage |

## bootstrap/ — Initialization & Shutdown

| File | Type | Description |
|------|------|-------------|
| [database-initialization-flow.md](bootstrap/database-initialization-flow.md) | Flowchart | Database constructor: in-memory vs persistent paths, WAL replay on startup |
| [database-shutdown-flow.md](bootstrap/database-shutdown-flow.md) | Flowchart | Shutdown: close connections, flush WAL, optional checkpoint, release buffer pool |

## query-processing/ — Query Engine

| File | Type | Description |
|------|------|-------------|
| [query-execution-flow.md](query-processing/query-execution-flow.md) | Flowchart | Full path from Connection::Query() through parse → bind → optimize → physical plan → execute |
| [parsing-flow.md](query-processing/parsing-flow.md) | Flowchart | Hand-written recursive descent parser: tokenize → parse → build ParsedStatement AST |
| [planner-binding-flow.md](query-processing/planner-binding-flow.md) | Flowchart | Binder resolves table/column references via Catalog → builds typed LogicalPlan |
| [optimizer-pipeline-flow.md](query-processing/optimizer-pipeline-flow.md) | Flowchart | Simplified optimizer: constant folding, predicate pushdown, column pruning, filter merge, join reordering |

## parallel/ — Execution Orchestration

| File | Type | Description |
|------|------|-------------|
| [pipeline-execution-flow.md](parallel/pipeline-execution-flow.md) | Flowchart | Executor::Execute() initializes and runs pipelines in order; single-threaded query execution |
| [pipeline-executor-flow.md](parallel/pipeline-executor-flow.md) | Flowchart | PipelineExecutor processes DataChunks: Source → Operators → Sink, chunk-at-a-time |
| [task-scheduler-flow.md](parallel/task-scheduler-flow.md) | Flowchart | Thread pool for background tasks (checkpoint, compaction); worker threads pull from task queue |

## storage/ — Storage Layer

| File | Type | Description |
|------|------|-------------|
| [buffer-manager-flow.md](storage/buffer-manager-flow.md) | Flowchart | BufferManager::Pin(): check memory → evict (LRU) if needed → load from BlockFile → return buffer |
| [wal-write-flow.md](storage/wal-write-flow.md) | Flowchart | On commit: serialize UndoBuffer entries to WAL with format [type][size][data], fsync |
| [wal-replay-flow.md](storage/wal-replay-flow.md) | Flowchart | On startup: read WAL → replay entries past last checkpoint → re-checkpoint |
| [checkpoint-flow.md](storage/checkpoint-flow.md) | Flowchart | Checkpoint: acquire lock → flush ColumnChunks → write Catalog snapshot → truncate WAL |
| [checkpoint-sequence.md](storage/checkpoint-sequence.md) | Sequence | Component interactions during checkpoint |
| [column-chunk-flow.md](storage/column-chunk-flow.md) | Flowchart | Column storage layout: Table → RowGroups → ColumnChunks → ColumnSegments → BlockFile blocks |
| [column-compression-flow.md](storage/column-compression-flow.md) | Flowchart | Compression selection at write time (RLE, bit-packing, delta, dictionary) and decompression at read time |

## transaction/ — Transaction System

| File | Type | Description |
|------|------|-------------|
| [transaction-lifecycle-flow.md](transaction/transaction-lifecycle-flow.md) | Flowchart | Auto-commit vs explicit BEGIN/COMMIT; TransactionManager assigns tx_id and start_time |
| [commit-flow.md](transaction/commit-flow.md) | Flowchart | Commit: write WAL → assign commit_time → apply UndoBuffer to main storage → optional checkpoint |
| [rollback-flow.md](transaction/rollback-flow.md) | Flowchart | Rollback: traverse UndoBuffer in reverse → undo each change → discard local storage |
| [mvcc-visibility-flow.md](transaction/mvcc-visibility-flow.md) | Flowchart | MVCC snapshot isolation: delete/insert/update visibility rules, version chain traversal, GC |

## data/ — Catalog & Data Operations

| File | Type | Description |
|------|------|-------------|
| [catalog-lookup-flow.md](data/catalog-lookup-flow.md) | Flowchart | Catalog::GetEntry(): schema lookup → MVCC visibility check → return CatalogEntry or error |
| [table-scan-flow.md](data/table-scan-flow.md) | Flowchart | Column-oriented scan: iterate RowGroups → load ColumnChunks → decompress → MVCC filter → DataChunk |
| [insert-data-flow.md](data/insert-data-flow.md) | Flowchart | INSERT: append to TransactionLocalStorage → on commit: WAL write + merge into main RowGroup |
| [delete-update-data-flow.md](data/delete-update-data-flow.md) | Flowchart | DELETE/UPDATE via MVCC version markers in VersionInfo + UndoBuffer; committed on transaction commit |

## execution/ — Vectorized Execution Details

| File | Type | Description |
|------|------|-------------|
| [vectorized-execution-flow.md](execution/vectorized-execution-flow.md) | Flowchart | DataChunk structure, Source fill, Filter predicate-over-vector, Projection, Sink accumulation |
| [join-execution-flow.md](execution/join-execution-flow.md) | Flowchart | Hash join (build + probe pipelines) and nested-loop join fallback |
| [aggregation-flow.md](execution/aggregation-flow.md) | Flowchart | Hash aggregation: build AggregateHashTable per chunk → finalize → emit result chunks |

## error/ — Error Handling

| File | Type | Description |
|------|------|-------------|
| [error-handling-flow.md](error/error-handling-flow.md) | Flowchart | Exception-based errors (ParseError, BindError, RuntimeError, IOError); auto-rollback on failure |

## monitoring/ — Observability

| File | Type | Description |
|------|------|-------------|
| [query-profiling-flow.md](monitoring/query-profiling-flow.md) | Flowchart | Per-phase timing (parse, bind, optimize, execute) and per-operator stats; output via EXPLAIN ANALYZE |

---

## Diagram Conventions

- **Flowcharts** use `flowchart TD` (top-down) and show branching control flow with decision nodes.
- **Assumptions** are listed at the top of each file.
- **Planned Implementation** sections in each file cite the source files that will implement the behavior.
- All diagrams describe CppColDB's planned architecture; none reference external databases.

## Total: 30 diagrams across 10 categories
