# Implementation Guide: CppColDB Build Order

## Purpose

This document tells you **what to build first, in what order, and what tests to pass before moving on**. Each feature has hard dependencies — you cannot implement the Binder before the Catalog exists, or the Executor before PhysicalOperator is defined. Follow the phases below strictly.

---

## Project Directory Layout

Create this structure before writing any code:

```
CppColDB/
├── CMakeLists.txt
├── src/
│   ├── common/             ← Phase 0: types, DataVector, DataChunk
│   ├── parser/             ← Phase 1: Tokenizer, Parser, AST nodes
│   │   └── ast/
│   ├── storage/            ← Phases 2–3
│   │   ├── column/
│   │   │   └── compression/
│   │   └── block_file.hpp
│   ├── catalog/            ← Phase 4
│   ├── transaction/        ← Phase 4
│   ├── wal/                ← Phase 2
│   ├── execution/          ← Phases 5–6
│   │   └── operators/
│   ├── planner/            ← Phase 7
│   │   ├── logical_plan/
│   │   └── physical_plan/
│   ├── checkpoint/         ← Phase 9
│   ├── scheduler/          ← Phase 9
│   ├── profiler/           ← Phase 10
│   └── main/               ← Phase 9: Database, Connection, ClientContext
├── test/
│   ├── common/
│   ├── parser/
│   ├── storage/
│   ├── catalog/
│   ├── transaction/
│   ├── execution/
│   ├── planner/
│   └── integration/
└── main.cpp                ← SQL REPL entry point (Phase 9)
```

---

## CMakeLists.txt Skeleton

```cmake
cmake_minimum_required(VERSION 3.20)
project(CppColDB VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Library ---
file(GLOB_RECURSE SOURCES "src/**/*.cpp")
add_library(cppcoldb_lib STATIC ${SOURCES})
target_include_directories(cppcoldb_lib PUBLIC src/)

# --- Main executable ---
add_executable(cppcoldb main.cpp)
target_link_libraries(cppcoldb PRIVATE cppcoldb_lib)

# --- Tests (one executable per phase; add as you go) ---
enable_testing()

function(add_phase_tests name sources)
  add_executable(${name} ${sources})
  target_link_libraries(${name} PRIVATE cppcoldb_lib)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_phase_tests(test_phase0 test/common/test_types.cpp test/common/test_data_chunk.cpp)
add_phase_tests(test_phase1 test/parser/test_tokenizer.cpp test/parser/test_parser.cpp)
# ... add more as you reach each phase
```

---

## Dependency Graph

```
core-types ──────────────────────────────────────────────────────┐
     │                                                            │
error-handling ──────────────────────────────────────────────────┤
     │                                                            │
     ├──► sql-parser                                              │
     │                                                            │
     ├──► buffer-manager ──► column-compression ──► column-storage│
     │                                                            │
     ├──► wal ──────────────────────────────────────────────────► catalog
     │                                                            │   │
     │                                               transaction ◄───┘
     │                                                    │
     │                                                    ▼
     ├──► physical-planner (PhysicalOperator base)        │
     │           │                                        │
     │    pipeline-executor ◄─────────────────────────────┤
     │           │                                        │
     │    vectorized-operators ◄──────────────────────────┤
     │                                                    │
     │    binder (depends: sql-parser + catalog + tx) ◄───┘
     │           │
     │    optimizer (depends: binder)
     │           │
     │    aggregation + join-execution (depends: pipeline-executor)
     │
     └──► checkpoint + task-scheduler ──► database ──► query-profiler
```

---

## Phase 0 — Foundation (start here)

**Goal**: define the types used everywhere. No I/O, no logic.

| Plan | Source files |
|------|-------------|
| `plan-core-types` | `src/common/types.hpp`, `src/common/data_vector.hpp`, `src/common/data_chunk.hpp` |
| `plan-error-handling` | `src/common/exception.hpp` |

**What to implement:**
1. `TypeId` enum (INT8 through VARCHAR, INVALID)
2. `Value` (std::variant + TypeId tag + is_null flag)
3. `DataVector` (typed array of STANDARD_VECTOR_SIZE=1024, std::bitset validity mask)
4. `DataChunk` (vector of DataVector, row count)
5. Exception hierarchy: `CppColDBException` → `ParseError`, `BindError`, `RuntimeError`, `IOError`

**Tests to pass before Phase 1:**
- `TestTypeSizes`: sizeof each primitive type
- `TestDataVectorFill`: fill a DataVector with 1024 INT64 values, read them back
- `TestDataChunkReset`: reset clears row count and validity bits
- `TestNullValidity`: set row 5 null, verify IsNull(5) == true, IsNull(6) == false
- `TestExceptionMessage`: throw ParseError("bad input"), catch CppColDBException, verify .what()

---

## Phase 1 — SQL Parser

**Depends on**: Phase 0 (core-types, error-handling)

| Plan | Source files |
|------|-------------|
| `plan-sql-parser` | `src/parser/tokenizer.hpp/cpp`, `src/parser/parser.hpp/cpp`, `src/parser/ast/*.hpp` |

**What to implement (in this order within the phase):**
1. `Token` struct and `TokenType` enum
2. `Tokenizer::Tokenize()` — character scan, keyword table, string/number/operator recognition
3. All AST node structs in `src/parser/ast/parsed_statement.hpp`
4. `Parser` helper methods: `Peek()`, `Consume()`, `Expect()`, `Check()`, `Match()`
5. `Parser::ParsePrimary()` through `Parser::ParseExpr()` (expression parsing, precedence levels)
6. `Parser::ParseSelect()`, `ParseInsert()`, `ParseUpdate()`, `ParseDelete()`
7. `Parser::ParseCreateTable()`, `ParseDropTable()`
8. `Parser::ParseTransaction()`, `ParseExplain()`
9. `Parser::Parse()` top-level dispatch

**Do NOT try to implement all statement types at once.** Implement and test SELECT first, then INSERT, then the rest.

**Tests to pass before Phase 2:**
- All tests from `plan-sql-parser` Section 11 (17 tests)
- Key blocker tests: `TestParseExprPrecedence`, `TestParseSelect`, `TestParseTrailingTokensError`

---

## Phase 2 — Storage Foundation

**Depends on**: Phase 0

| Plan | Source files |
|------|-------------|
| `plan-buffer-manager` | `src/storage/block_file.hpp/cpp`, `src/storage/buffer_manager.hpp/cpp` |
| `plan-wal` | `src/wal/wal.hpp/cpp` |
| `plan-column-compression` | `src/storage/column/compression/*.hpp/cpp` |

**Implement in this order within the phase:**

**2a. BlockFile** (no dependencies; simplest storage unit):
- `BlockFile::Open(path)`, `ReadBlock(id, buf)`, `WriteBlock(id, buf)`, `AllocateBlock() -> BlockId`
- File format: fixed-size blocks, block 0 = catalog header, blocks 1..N = data
- Test: write block 5, reopen file, read block 5, verify bytes match

**2b. BufferManager**:
- `Frame` struct (block_id, buffer, pin_count, is_dirty)
- `BufferManager::Pin(block_id) -> BufferHandle` (RAII, unpins on destructor)
- LRU eviction when pool is full
- Test: pin 3 blocks with pool_size=2 → verify oldest is evicted; test dirty write-back

**2c. WAL** (can be done in parallel with BufferManager):
- `WAL::Open(path)`, `AppendEntry(type, data)`, `Flush()`, `OpenForReplay()`, `ReadNextEntry()`
- Entry format: `[type: uint8][payload_size: uint32][payload: bytes]`
- Test: write 3 entries, close, reopen for replay, read all 3 back in order

**2d. Column Compression** (can be done in parallel with WAL):
- Implement `SelectCompression()`, `Compress()`, `Decompress()`
- Implement RLE first (simplest), then delta, then bit-packing (requires `WriteNBits`/`ReadNBits`), then dictionary
- **Critical**: implement and unit-test `WriteNBits`/`ReadNBits` IN ISOLATION before using them in `BitPackEncode`
- Test: for each codec, round-trip encode → decode → verify exact match

**Tests to pass before Phase 3:**
- BlockFile: read/write, multi-block, reopen
- BufferManager: eviction, dirty write-back, double-pin
- WAL: append/replay, empty file, partial write on crash (simulate by not flushing)
- Compression: all round-trip tests from `plan-column-compression` Section 11

---

## Phase 3 — Column Storage

**Depends on**: Phase 2 (BufferManager, compression)

| Plan | Source files |
|------|-------------|
| `plan-column-storage` | `src/storage/column/column_segment.hpp/cpp`, `src/storage/column/column_chunk.hpp/cpp`, `src/storage/column/row_group.hpp/cpp`, `src/storage/column/version_info.hpp/cpp` |

**Implement in this order:**
1. `ColumnSegment`: just metadata (block_id, compression_type, row_count, min/max stats) — no logic yet
2. `ColumnChunk`: owns ColumnSegments; implement `Scan(row_offset, count, out_vec)` and `Flush(vec)` using compression
3. `VersionInfo`: per-RowGroup structure for delete/update markers (used by MVCC in Phase 4)
4. `RowGroup`: owns one ColumnChunk per column; `Scan()`, `Append()`, `CommitAppend()`, `RevertAppend()`

**Key insight for juniors**: At this phase, `RowGroup::Scan()` does NOT do MVCC filtering yet — add that in Phase 4 when Transaction is available. For now, scan returns all rows unconditionally.

**Tests to pass before Phase 4:**
- `TestColumnChunkRoundTrip`: append 200 INT64 values, flush to block, scan back → exact match
- `TestColumnChunkNulls`: append with some nulls, scan back → nulls preserved
- `TestRowGroupAppendScan`: append 3 rows across 2 columns, scan → correct DataChunk
- `TestRowGroupMultipleSegments`: append > block capacity → spans two segments

---

## Phase 4 — Catalog + Transaction

**Depends on**: Phase 0, Phase 3 (RowGroup), Phase 2 (WAL)

These two features are tightly coupled and must be built together.

| Plan | Source files |
|------|-------------|
| `plan-catalog` | `src/catalog/catalog.hpp/cpp`, `src/catalog/schema.hpp/cpp`, `src/catalog/table_catalog_entry.hpp/cpp` |
| `plan-transaction` | `src/transaction/transaction.hpp/cpp`, `src/transaction/transaction_manager.hpp/cpp`, `src/transaction/undo_buffer.hpp/cpp` |

**Implement in this order:**
1. `TableCatalogEntry` (column definitions, pointer to table's RowGroup list)
2. `Schema` (map of table name → TableCatalogEntry)
3. `Catalog` (map of schema name → Schema; `CreateTable`, `DropTable`, `GetEntry`)
   — First implement WITHOUT MVCC (just a map lookup). Add MVCC in step 5.
4. `UndoBuffer` (vector of UndoEntry variants; ForEachForward, ForEachReverse)
5. `Transaction` (tx_id, start_time, commit_time, undo_buffer, local_storage)
6. `TransactionManager::BeginTransaction`, `Commit`, `Rollback`
7. Add MVCC visibility to `Catalog::GetEntry` (check entry's create_txid and commit_time)
8. Add MVCC row visibility to `RowGroup::Scan` (check VersionInfo per row)

**Tests to pass before Phase 5:**
- `TestCatalogCreateLookup`: create table, look it up → found
- `TestCatalogDropTable`: create then drop → not found
- `TestAutoCommit`: insert rows, auto-commit → visible in new transaction
- `TestExplicitRollback`: BEGIN, insert rows, ROLLBACK → rows gone
- `TestMVCCSnapshotIsolation`: T1 inserts; T2 started before T1 commits → T2 cannot see T1's rows
- `TestWriteWriteConflict`: two transactions update same row → second throws RuntimeError

---

## Phase 5 — Execution Foundation

**Depends on**: Phase 0

| Plan | Source files |
|------|-------------|
| `plan-pipeline-executor` | `src/execution/physical_operator.hpp`, `src/execution/operator_result_type.hpp`, `src/execution/pipeline.hpp`, `src/execution/executor.hpp/cpp`, `src/execution/pipeline_executor.hpp/cpp` |

**What to implement:**
1. `OperatorResultType` enum (FINISHED, HAVE_MORE_OUTPUT, NEEDS_MORE_INPUT) — see plan Section 5
2. `PhysicalOperator` base class with virtual `GetData`, `Execute`, `Consume`, `Finalize`, `TryFlush`
3. `OperatorState` base class + `ScanState`
4. `Pipeline` struct (source*, operators[], sink*, dependencies[])
5. `Executor`: `Initialize(plan)`, `Execute()`, `GetResult()`; `BuildPipelines()` recursive walk
6. `PipelineExecutor::Execute()` loop — implement EXACTLY as in the plan Section 7 pseudocode, including the `HAVE_MORE_OUTPUT` inner loop and `TryFlushOperators()`
7. `QueryResult` struct (column_names, column_types, chunks, success, error_message)
8. `PhysicalResultCollector`: a sink that appends DataChunks to a QueryResult

**Junior trap**: do NOT skip the `HAVE_MORE_OUTPUT` inner loop. Even if early operators don't use it, it must be there for aggregation and hash join to work in Phase 8.

**Tests to pass before Phase 6:**
- Build a toy `PhysicalConstantScan` that returns 3 hardcoded rows across 2 calls
- `TestPipelineExecutorBasic`: ConstantScan → ResultCollector → 3 rows in result
- `TestPipelineExecutorOperatorChain`: ConstantScan → toy IdentityOperator → ResultCollector → same 3 rows
- `TestPipelineExecutorNEEDS_MORE_INPUT`: toy operator that buffers 2 chunks then emits 1 → verify buffering
- `TestPipelineExecutorHAVE_MORE_OUTPUT`: toy operator that returns 2 output chunks for 1 input → verify both chunks reach sink

---

## Phase 6 — Basic Operators

**Depends on**: Phase 3 (column-storage), Phase 4 (catalog, transaction), Phase 5 (executor)

| Plan | Source files |
|------|-------------|
| `plan-vectorized-operators` | `src/execution/operators/physical_table_scan.hpp/cpp`, `physical_filter.hpp/cpp`, `physical_projection.hpp/cpp`, `physical_limit.hpp/cpp` |

**What to implement (in order):**
1. `PhysicalTableScan`: calls RowGroup::Scan() with MVCC; emits DataChunks; uses ScanState
2. `PhysicalFilter`: applies a boolean expression to each row using a selection vector
3. `PhysicalProjection`: evaluates expression list over input chunk, emits output with new column layout
4. `PhysicalLimit`: passes rows through until limit reached, then returns FINISHED

**Expression evaluation** — you need a simple `ExpressionExecutor` that takes a `LogicalExpr` tree and evaluates it against a DataChunk:
- `EvalExpr(expr, input_chunk, output_vector)`:
  - BoundColumnRef: copy the column from input
  - LogicalLit: fill the output vector with a constant
  - LogicalBinaryOp: evaluate left and right, apply op element-wise
- This is used by both PhysicalFilter and PhysicalProjection

**Tests to pass before Phase 7:**
- `TestTableScanReturnsRows`: create table, insert 5 rows, scan → 5 rows with correct values
- `TestTableScanMVCC`: T1 inserts rows not committed; T2 scans → T2 sees 0 rows
- `TestFilterKeepsMatchingRows`: scan + filter `a > 3` on [1,2,3,4,5] → [4,5]
- `TestProjectionSelectsColumn`: scan + project single column out of 3 → 1-column result
- `TestLimitStopsEarly`: 1000 rows, LIMIT 10 → exactly 10 rows, scan exits early

---

## Phase 7 — Query Planner

**Depends on**: Phase 1 (parser), Phase 4 (catalog, transaction), Phase 6 (operators)

| Plan | Source files |
|------|-------------|
| `plan-binder` | `src/planner/binder.hpp/cpp`, `src/planner/bind_context.hpp`, `src/planner/logical_plan/*.hpp` |
| `plan-optimizer` | `src/planner/optimizer.hpp/cpp` |
| `plan-physical-planner` | `src/planner/physical_planner.hpp/cpp`, `src/planner/physical_plan/*.hpp` |

**Implement in this order:**
1. All `LogicalPlan` node structs (`LogicalGet`, `LogicalFilter`, `LogicalProjection`, `LogicalInsert`, etc.)
2. `BindContext::AddTable`, `ResolveColumn`, `ResolveQualified`
3. `Binder::BindSelect` — start with the simple case: no aggregates, no ORDER BY, no GROUP BY
4. `Binder::BindExpr` and `InferBinaryType` / `Coerce`
5. `Binder::BindInsert`, `BindUpdate`, `BindDelete`, `BindCreateTable`, `BindDropTable`
6. Add SELECT * expansion (ExpandStar)
7. Add GROUP BY / aggregate binding
8. Add ORDER BY binding
9. `Optimizer` — implement passes in order: constant folding → predicate pushdown → column pruning → filter merge → join reordering (join reordering can be deferred until Phase 8)
10. `PhysicalPlanner::Plan(LogicalPlan)` — convert each logical node to a physical operator

**Do NOT implement multi-table JOIN binding yet** — that is Phase 8.

**Tests to pass before Phase 8:**
- All binder tests from `plan-binder` Section 11
- End-to-end: parse → bind → optimize → physical plan → execute for:
  - `SELECT * FROM t`
  - `SELECT a FROM t WHERE a > 5`
  - `SELECT COUNT(*) FROM t`
  - `INSERT INTO t VALUES (1, 'hello')`
  - `CREATE TABLE t (id INT64, name VARCHAR)`

---

## Phase 8 — Advanced Operators

**Depends on**: Phase 5 (executor), Phase 7 (planner)

| Plan | Source files |
|------|-------------|
| `plan-aggregation` | `src/execution/operators/physical_hash_aggregation.hpp/cpp`, `src/execution/aggregate_hash_table.hpp/cpp` |
| `plan-join-execution` | `src/execution/operators/physical_hash_join.hpp/cpp`, `src/execution/hash_table.hpp/cpp` |

**Implement aggregation before join** — aggregation is simpler because it's a single pipeline.

**Aggregation:**
1. `AggregateHashTable`: `std::unordered_map<GroupKey, AggregateState>`, vectorized `AddChunk()`, `Finalize()`
2. `PhysicalHashAggregation`: NEEDS_MORE_INPUT during consume phase; HAVE_MORE_OUTPUT during finalize
3. Test: `SELECT dept, SUM(salary) FROM t GROUP BY dept`

**Hash Join (two-pipeline):**
1. `HashTable` (build phase): `std::unordered_multimap<KeyHash, DataChunk row>`, `Insert()`, `Probe()`
2. `PhysicalHashJoinBuild`: sink that populates the HashTable; stores it in shared state between pipelines
3. `PhysicalHashJoinProbe`: source/operator that probes the HashTable; can return HAVE_MORE_OUTPUT when one probe key matches multiple build rows
4. `Binder::BindSelect` join extension: handle `FROM t1 JOIN t2 ON ...` parsing/binding
5. Test: `SELECT a.id, b.name FROM a JOIN b ON a.id = b.id`

**Tests to pass before Phase 9:**
- All aggregation tests from `plan-aggregation` Section 11
- All join tests from `plan-join-execution` Section 11
- `TestGroupByMultipleKeys`: GROUP BY two columns
- `TestJoinNullHandling`: join key has NULL rows → not matched

---

## Phase 9 — Database Bootstrap

**Depends on**: everything above

| Plan | Source files |
|------|-------------|
| `plan-checkpoint` | `src/checkpoint/checkpoint_manager.hpp/cpp` |
| `plan-task-scheduler` | `src/scheduler/task_scheduler.hpp/cpp` |
| `plan-database` | `src/main/database.hpp/cpp`, `src/main/connection.hpp/cpp`, `src/main/client_context.hpp/cpp` |

**Implement in this order:**
1. `TaskScheduler`: thread pool with `Submit(task)`, `Shutdown()`; start with 1 worker thread
2. `CheckpointManager::CreateCheckpoint()`: flush dirty ColumnChunks, serialize Catalog to block 0, truncate WAL
3. `ClientContext`: owns Parser, Binder, Optimizer, PhysicalPlanner, Executor; `Query(sql)` drives the full pipeline
4. `Connection`: wraps ClientContext; `Query()`, `Begin()`, `Commit()`, `Rollback()`
5. `Database` in-memory mode: wire all subsystems together with null WAL and no BlockFile
6. `Database` persistent mode: open/create BlockFile, initialize WAL, LoadFromCheckpoint if block 0 exists, ReplayWAL if WAL exists
7. `main.cpp` SQL REPL: read line → `conn.Query(line)` → print result

**Tests to pass before Phase 10:**
- All database tests from `plan-database` Section 11
- Integration: open persistent DB → create table → insert rows → close → reopen → query rows → same data

---

## Phase 10 — Query Profiler

**Depends on**: Phase 9

| Plan | Source files |
|------|-------------|
| `plan-query-profiler` | `src/profiler/query_profiler.hpp/cpp` |

**What to implement:**
1. `QueryProfiler`: store `phase_timings` (parse/bind/optimize/execute), `operator_timings`
2. `OperatorProfileGuard`: RAII timer that records elapsed time for one operator
3. Wire into `ClientContext::Query()`: wrap each phase with a timer
4. `EXPLAIN ANALYZE SELECT ...` → execute query + return profiling output as QueryResult

**Tests:**
- `TestProfilerRecordsTiming`: run a query, check all phases have non-zero time
- `TestExplainAnalyze`: `EXPLAIN ANALYZE SELECT * FROM t` returns timing rows

---

## Integration Tests (after all phases)

Write these once Phase 9 is complete. Each test uses `Database` + `Connection`.

```
TestEndToEndInMemory:
  db = Database(":memory:")
  conn = db.Connect()
  conn.Query("CREATE TABLE orders (id INT64, amount FLOAT64)")
  conn.Query("INSERT INTO orders VALUES (1, 99.5), (2, 200.0), (3, 50.0)")
  result = conn.Query("SELECT id, amount FROM orders WHERE amount > 60.0")
  assert result.RowCount() == 2   // rows 1 and 2
  assert result[0]["id"]  == 1
  assert result[1]["id"]  == 2

TestEndToEndPersistent:
  // Session 1
  db = Database("/tmp/test.db")
  conn = db.Connect()
  conn.Query("CREATE TABLE t (x INT64)")
  conn.Query("INSERT INTO t VALUES (42)")
  // destroy db (triggers checkpoint + WAL flush)

  // Session 2: reopen
  db2 = Database("/tmp/test.db")
  conn2 = db2.Connect()
  result = conn2.Query("SELECT x FROM t")
  assert result[0]["x"] == 42    // survived restart

TestEndToEndTransaction:
  db = Database(":memory:")
  conn1 = db.Connect()
  conn2 = db.Connect()
  conn1.Begin()
  conn1.Query("INSERT INTO t VALUES (99)")
  result = conn2.Query("SELECT * FROM t")   // T2 started before T1 commits
  assert result.RowCount() == 0             // MVCC: T2 cannot see T1's uncommitted row
  conn1.Commit()
  result = conn2.Query("SELECT * FROM t")
  assert result.RowCount() == 1             // now T2 can see it (new snapshot)

TestEndToEndGroupBy:
  conn.Query("CREATE TABLE sales (dept VARCHAR, amount INT64)")
  conn.Query("INSERT INTO sales VALUES ('eng',100),('eng',200),('hr',50)")
  result = conn.Query("SELECT dept, SUM(amount) FROM sales GROUP BY dept ORDER BY dept")
  assert result.RowCount() == 2
  assert result[0] == ["eng", 300]
  assert result[1] == ["hr", 50]

TestEndToEndJoin:
  conn.Query("CREATE TABLE a (id INT64, name VARCHAR)")
  conn.Query("CREATE TABLE b (id INT64, salary INT64)")
  conn.Query("INSERT INTO a VALUES (1,'alice'),(2,'bob')")
  conn.Query("INSERT INTO b VALUES (1,1000),(2,2000)")
  result = conn.Query("SELECT a.name, b.salary FROM a JOIN b ON a.id = b.id")
  assert result.RowCount() == 2
```

---

## Common Junior Mistakes to Avoid

1. **Don't implement Phase N+1 before Phase N tests pass.** Each phase builds on the correctness of the previous one. A bug in DataVector will corrupt every operator and be nearly impossible to trace later.

2. **Don't try to implement WriteNBits by guessing.** Read the pseudocode in `plan-column-compression` Section 7 line by line, then write a unit test with the concrete example `[5, 3, 7]` before using it in BitPackEncode.

3. **Don't skip the HAVE_MORE_OUTPUT inner loop in PipelineExecutor.** It looks like dead code for simple queries — but hash join and aggregation break without it.

4. **Don't add MVCC to RowGroup::Scan until Transaction is implemented (Phase 4).** Leave a `// TODO: add MVCC filter here` comment and come back to it.

5. **DataChunk.Reset() must zero the count and clear validity bits.** If you forget this, operators reuse stale data from previous iterations and produce garbage results.

6. **The pipeline dependency order matters.** The hash join build pipeline must complete before the probe pipeline runs. The `Executor::BuildPipelines()` + topological sort in `plan-pipeline-executor` Section 7 is what ensures this.

7. **WAL writes must be fsynced before Commit() returns.** If you skip fsync in tests it appears to work, but the WAL replay test will fail because the OS may not have flushed data to disk.
