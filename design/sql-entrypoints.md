# SQL Entry Points and Session Lifecycle

## Purpose

Provide the public runtime surface for CppColDB (REPL and embedded API), own subsystem startup/shutdown, and drive one-statement SQL execution through `Connection` and `ClientContext`.

## Scope

**In scope:**
- CLI entrypoint in `main.cpp` that accepts SQL lines.
- `Database` initialization for in-memory and persistent modes.
- Per-connection transaction lifecycle (`BEGIN` / `COMMIT` / `ROLLBACK`, auto-commit).
- End-to-end query dispatch from `Connection::Query` to `ClientContext::Query`.

**Out of scope:**
- SQL grammar and AST construction (`src/parser`).
- Binding/planning/execution operator details (`src/planner`, `src/execution`).
- On-disk encoding internals (`src/storage`).

## Primary User Flow

1. Caller constructs `Database(path, config)`.
2. Caller gets a `Connection` via `Database::Connect()`.
3. Caller executes SQL with `Connection::Query(sql)`.
4. `Connection` starts/uses a transaction and delegates to `ClientContext::Query`.
5. Caller receives `QueryResult` with `success`, schema, and chunks.
6. On shutdown, `Database::~Database()` flushes/checkpoints persistent state and stops subsystems.

## System Flow

1. CLI path starts in `main.cpp`, constructs `Database`, then `Connect()`, then loops on `conn->Query(line)`.
2. `Database::Database` branches into `InitInMemory()` or `InitPersistent()` in `src/main/database.cpp`.
3. `Connection::Connection` wires `ctx_.catalog`, `ctx_.txn_manager`, optional `ctx_.wal`, and registers itself (`RegisterConnection`).
4. `Connection::Query` intercepts standalone `BEGIN`/`COMMIT`/`ROLLBACK`; otherwise it starts auto-commit tx when needed and sets `ctx_.transaction`.
5. `ClientContext::Query` runs parse -> bind -> optimize -> physical plan -> execute and returns `QueryResult`.
6. Auto-commit path commits on success and rolls back on error in `Connection::Query`.
7. `Database::Shutdown` flushes WAL, checkpoints, shuts down scheduler, then flushes buffer manager.

```
CLI / embedding caller
  -> Database(path)
     -> InitInMemory | InitPersistent
  -> Connection::Query(sql)
     -> (optional tx keyword intercept)
     -> begin tx if auto-commit
     -> ClientContext::Query
        -> parser -> binder -> optimizer -> physical planner -> executor
     -> commit | rollback (auto-commit path)
  -> Database::Shutdown
```

## Data Model

- `DatabaseConfig` (`src/main/database.hpp`) fields:
- `buffer_pool_bytes (size_t)`
- `block_size (size_t)`
- `checkpoint_threshold_bytes (size_t)`
- `task_scheduler_threads (size_t)`
- `Database` state (`src/main/database.hpp`):
- `path_ (std::string)`, `is_in_memory_ (bool)`
- optional persistent subsystems: `block_file_`, `wal_`, `ckpt_manager_`
- shared subsystems: `buffer_manager_`, `catalog_`, `txn_manager_`, `scheduler_`
- open connection tracking: `open_connections_` guarded by `connections_mu_`
- `Connection` state (`src/main/connection.hpp`):
- `db_`, `ctx_`, `active_tx_ (std::shared_ptr<Transaction>)`
- `ClientContext` (`src/main/client_context.hpp`):
- pointers: `catalog`, `transaction`, `txn_manager`, `wal`
- profiling toggles/state: `profiling_enabled_`, `profiler_`

## Interfaces and Contracts

- `main.cpp:int main(int argc, char* argv[])`
- contract: starts REPL; `:memory:` default path; exits on `exit|quit|\q`.
- `Database::Database(const std::string&, DatabaseConfig)` (`src/main/database.cpp`)
- contract: initializes storage/catalog/tx/scheduler; replays WAL in persistent mode.
- `std::unique_ptr<Connection> Database::Connect()`
- contract: returns a new registered connection.
- `QueryResult Connection::Query(const std::string&)` (`src/main/connection.cpp`)
- contract: transaction keywords must be standalone statements; non-keyword SQL goes through pipeline.
- `void Connection::Begin/Commit/Rollback()`
- contract: throws `RuntimeError` for invalid tx state transitions.

## Dependencies

**Internal modules:**
- `src/parser`, `src/planner`, `src/execution` - query pipeline from `ClientContext::Query`.
- `src/catalog`, `src/transaction` - catalog lookup and transaction management.
- `src/storage` and `src/checkpoint` - persistence and recovery lifecycle.
- `src/parallel` - scheduler startup/shutdown.

**External services/libraries:**
- POSIX file APIs via storage modules (`open`, `pread`, `pwrite`, `fsync`) when persistent mode is enabled.

## Failure Modes and Edge Cases

- `BEGIN`, `COMMIT`, `ROLLBACK` only work when they are the whole statement in `Connection::Query`; `BEGIN TRANSACTION` is rejected as normal SQL.
- Explicit transaction mode does not auto-rollback after a failed statement; transaction remains open until user issues `ROLLBACK` or `COMMIT`.
- `Database::GetWAL()` throws `RuntimeError` in in-memory mode (`wal_ == nullptr`).
- `ClientContext::Query` catches `CppColDBException` and `std::exception`, returning `QueryResult{success=false,error_message=...}`.
- `Database::ReplayWAL` ignores schema-level WAL entries (`WAL_CREATE_SCHEMA`, `WAL_DROP_SCHEMA`) in default switch branch.

## Observability and Debugging

- REPL prints SQL errors from `QueryResult.error_message` in `main.cpp`.
- Pipeline stages are visible through `EXPLAIN` and `EXPLAIN ANALYZE` handled in `ClientContext::Query`.
- Debugging entry points:
- startup/recovery: `src/main/database.cpp:Database::InitPersistent` and `ReplayWAL`
- transaction behavior: `src/main/connection.cpp:Connection::Query`

## Risks and Notes

- `checkpoint_threshold_bytes` is defined in `DatabaseConfig` but not used to trigger checkpoints.
- `Database::Shutdown` swallows exceptions while flushing/checkpointing, so shutdown failures are silent unless manually instrumented.

Changes:

