# Feature: Error Handling

## 1. Purpose

CppColDB uses C++ exceptions for error propagation. Four concrete exception types cover all error categories: `ParseError` (bad SQL syntax), `BindError` (unknown table/column/type mismatch), `RuntimeError` (execution failures such as division by zero, constraint violations, out of memory), and `IOError` (disk and storage failures). Errors are caught at the top-level `ClientContext::Query()` boundary, the transaction is rolled back if needed, and the error is returned to the caller as a failed `QueryResult` rather than propagating further.

## 2. Responsibilities

- Define `CppColDBException` as a common base exception with a `message` field
- Define `ParseError`, `BindError`, `RuntimeError`, `IOError` as concrete subtypes
- Each exception carries a human-readable message and, where useful, position information (for `ParseError`: byte offset in the SQL string)
- `ClientContext::Query()`: catch all `CppColDBException` subtypes; build an error `QueryResult`; decide whether to roll back the active transaction
- Auto-commit transaction: always roll back on any exception
- Explicit transaction: roll back only on `RuntimeError` and `IOError` (which invalidate the transaction); `ParseError` and `BindError` leave the transaction open for the client to retry
- Mark the `Transaction::is_invalid` flag on `RuntimeError` / `IOError` so the client must issue `ROLLBACK` before running another statement

## 3. Non-Responsibilities

- Does not use error codes or `std::expected` (exception-only model)
- Does not log errors (logging is a higher-level concern outside this feature)
- Does not provide retry logic for transient `IOError`s
- Does not implement structured diagnostics (error codes, SQL state) — messages only
- Does not propagate errors across a network boundary

## 4. Architecture Design

```
Exception raised anywhere during query processing:
  ParseError     — from Tokenizer or Parser
  BindError      — from Binder
  RuntimeError   — from Executor, operators, constraint checks
  IOError        — from BufferManager, WAL, BlockFile

                             ↓ propagates up call stack
ClientContext::Query(sql)
  try:
    parse → bind → optimize → physical plan → execute
  catch (ParseError& e):
    if auto_commit: Rollback()
    else:           leave transaction open (parse errors don't corrupt state)
    return ErrorResult(e.message, "ParseError")

  catch (BindError& e):
    if auto_commit: Rollback()
    else:           leave transaction open
    return ErrorResult(e.message, "BindError")

  catch (RuntimeError& e):
    Rollback()
    tx.is_invalid = true  (for explicit transactions)
    return ErrorResult(e.message, "RuntimeError")

  catch (IOError& e):
    Rollback()
    tx.is_invalid = true
    return ErrorResult(e.message, "IOError")

  catch (std::exception& e):
    Rollback()
    return ErrorResult(e.what(), "InternalError")
```

**Transaction invalidation rules:**

| Error Type | Auto-commit | Explicit transaction |
|------------|-------------|----------------------|
| ParseError | Roll back (no-op, nothing changed) | Leave open (no state corrupted) |
| BindError  | Roll back (no-op, nothing changed) | Leave open |
| RuntimeError | Roll back | Roll back + set is_invalid |
| IOError    | Roll back | Roll back + set is_invalid |

## 5. Core Data Structures (C++)

```cpp
// src/common/exception.hpp
#pragma once
#include <stdexcept>
#include <string>
#include <optional>
#include <cstddef>

namespace cppcoldb {

// Base exception for all CppColDB errors.
class CppColDBException : public std::exception {
public:
    explicit CppColDBException(std::string msg)
        : message_(std::move(msg)) {}

    const char* what() const noexcept override { return message_.c_str(); }
    const std::string& Message() const { return message_; }

    virtual std::string Type() const = 0;

protected:
    std::string message_;
};

// SQL syntax error from the Tokenizer or Parser.
class ParseError : public CppColDBException {
public:
    ParseError(std::string msg, size_t pos = std::string::npos)
        : CppColDBException(std::move(msg)), position_(pos) {}

    std::string Type() const override { return "ParseError"; }

    // Byte offset in the original SQL string where the error was detected.
    // std::string::npos if position is not available.
    size_t Position() const { return position_; }

private:
    size_t position_;
};

// Semantic error from the Binder.
class BindError : public CppColDBException {
public:
    explicit BindError(std::string msg)
        : CppColDBException(std::move(msg)) {}

    std::string Type() const override { return "BindError"; }
};

// Execution-time error: constraint violation, division by zero, OOM, type cast failure.
class RuntimeError : public CppColDBException {
public:
    explicit RuntimeError(std::string msg)
        : CppColDBException(std::move(msg)) {}

    std::string Type() const override { return "RuntimeError"; }
};

// Storage / I/O error: block read/write failure, WAL write failure, corrupt file.
class IOError : public CppColDBException {
public:
    explicit IOError(std::string msg)
        : CppColDBException(std::move(msg)) {}

    std::string Type() const override { return "IOError"; }
};

// Convenience macros for common error patterns.
#define THROW_PARSE_ERROR(msg, pos)    throw ::cppcoldb::ParseError((msg), (pos))
#define THROW_BIND_ERROR(msg)          throw ::cppcoldb::BindError(msg)
#define THROW_RUNTIME_ERROR(msg)       throw ::cppcoldb::RuntimeError(msg)
#define THROW_IO_ERROR(msg)            throw ::cppcoldb::IOError(msg)

} // namespace cppcoldb
```

## 6. Public Interfaces

```cpp
// src/common/exception.hpp
namespace cppcoldb {

class CppColDBException : public std::exception {
public:
    const char*        what()    const noexcept override;
    const std::string& Message() const;
    virtual std::string Type()   const = 0;
};

class ParseError   : public CppColDBException { size_t Position() const; };
class BindError    : public CppColDBException {};
class RuntimeError : public CppColDBException {};
class IOError      : public CppColDBException {};

} // namespace cppcoldb

// src/main/client_context.hpp
namespace cppcoldb {
class ClientContext {
public:
    QueryResult Query(const std::string& sql);
    // ^ catches all CppColDBException; handles rollback; returns error QueryResult on failure
};
} // namespace cppcoldb
```

## 7. Internal Algorithms

### ClientContext::Query error handling
```
Query(sql):
  active_tx = nullptr
  try:
    if not in_explicit_transaction:
      active_tx = txn_mgr_.BeginTransaction(/*auto_commit=*/true)
    else:
      active_tx = current_explicit_transaction_

    parsed   = parser.Parse(sql)
    plan     = planner.Plan(parsed)
    opt_plan = optimizer.Optimize(plan)
    phy_plan = physical_planner.Plan(opt_plan)
    executor.Initialize(phy_plan)
    executor.Execute()
    result = executor.GetResult()

    if active_tx->auto_commit:
      txn_mgr_.Commit(active_tx)
    return result

  catch (ParseError& e):
    if active_tx AND active_tx->auto_commit:
      txn_mgr_.Rollback(active_tx)
    // Explicit txn: leave open (parse failed before any state change)
    return MakeErrorResult(e.Message(), e.Type())

  catch (BindError& e):
    if active_tx AND active_tx->auto_commit:
      txn_mgr_.Rollback(active_tx)
    return MakeErrorResult(e.Message(), e.Type())

  catch (RuntimeError& e):
    if active_tx:
      txn_mgr_.Rollback(active_tx)
      if not active_tx->auto_commit:
        active_tx->is_invalid = true  // client must ROLLBACK
    return MakeErrorResult(e.Message(), e.Type())

  catch (IOError& e):
    if active_tx:
      txn_mgr_.Rollback(active_tx)
      if not active_tx->auto_commit:
        active_tx->is_invalid = true
    return MakeErrorResult(e.Message(), e.Type())

  catch (std::exception& e):
    if active_tx: txn_mgr_.Rollback(active_tx)
    return MakeErrorResult(std::string("Internal error: ") + e.what(), "InternalError")
```

### Rollback safety on IOError
```
On IOError during WAL::Flush():
  The transaction has not yet been applied to storage (WAL-first guarantee).
  Rollback() traverses UndoBuffer in reverse: nothing was applied, so each undo
  operation is a no-op or trivially safe (RevertAppend removes pending rows from
  local storage, VersionInfo markers are cleared).
  The database remains consistent.
```

### Invalid transaction guard
```
// Called at the start of each statement in an explicit transaction:
if current_explicit_transaction_->is_invalid:
  throw RuntimeError("Transaction is in an invalid state. Issue ROLLBACK.")
// This prevents additional work being done on a corrupted transaction.
```

## 8. Persistence Model

Not applicable. Exceptions are in-memory objects; no exception data is persisted.

## 9. Concurrency Model

Exceptions propagate on the thread that threw them. No cross-thread exception passing. Each `ClientContext` (and its active transaction) is owned by exactly one thread.

## 10. Configuration

No runtime configuration for the exception hierarchy itself.

```cpp
// Error messages can include context-specific details:
// ParseError: "unexpected token 'GARBAGE' at position 12"
// BindError:  "table 'orders' not found in schema 'main'"
// RuntimeError: "division by zero in expression: a / b"
// IOError:    "failed to write WAL block: disk full"
```

## 11. Testing Strategy

- `TestParseErrorThrown`: malformed SQL → `ParseError` caught at Query boundary
- `TestParseErrorPosition`: `ParseError::Position()` returns the correct byte offset
- `TestBindErrorTableNotFound`: `SELECT * FROM nonexistent` → `BindError`
- `TestBindErrorColumnNotFound`: `SELECT zzz FROM t` → `BindError`
- `TestRuntimeErrorDivByZero`: `SELECT 1/0` → `RuntimeError`
- `TestRuntimeErrorConstraint`: INSERT violating NOT NULL → `RuntimeError`
- `TestIOErrorWALWrite`: mock WAL::Flush() to fail → `IOError` thrown, transaction rolled back
- `TestAutoCommitRollbackOnParseError`: auto-commit + parse failure → transaction rolled back, no partial state
- `TestAutoCommitRollbackOnRuntimeError`: auto-commit + runtime failure → transaction rolled back
- `TestExplicitTxnParseErrorLeavesOpen`: BEGIN + parse failure → transaction still active, can retry
- `TestExplicitTxnRuntimeErrorInvalidates`: BEGIN + runtime error → `is_invalid == true`, further statements throw
- `TestExplicitTxnMustRollback`: invalid explicit transaction → next statement throws "Transaction invalid"
- `TestExceptionTypeNames`: each exception class returns correct `Type()` string
- `TestStdExceptionCaught`: non-CppColDB exception thrown by operator → caught as InternalError

## 12. Open Questions

None.
