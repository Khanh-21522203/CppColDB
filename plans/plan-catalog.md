# Feature: Catalog

## 1. Purpose

The Catalog is the in-memory schema registry for the database. It stores all schemas, tables, and column definitions. Catalog entries are MVCC-aware: each `CatalogEntry` carries `create_time` and `delete_time` timestamps so that concurrent transactions see a consistent snapshot of the schema. The Binder consults the Catalog to resolve table and column references; the CheckpointManager serializes the Catalog to disk during a checkpoint.

## 2. Responsibilities

- Maintain a two-level hierarchy: `Catalog` → `Schema` → `CatalogEntry` (tables, views)
- `Catalog::GetEntry(schema, name, tx)`: look up a named entry with MVCC visibility check; throw `BindError` or return `nullptr` depending on the `OnNotFound` policy
- `Catalog::CreateTable(schema, name, col_defs, tx)`: validate the table does not already exist; create a `TableCatalogEntry`; record the create transaction in the entry's `create_tx_id`
- `Catalog::DropTable(schema, name, tx)`: mark the entry's `delete_tx_id` so it is invisible after the transaction commits
- `Catalog::CreateSchema(name, tx)` / `Catalog::DropSchema(name, tx)`: schema lifecycle management
- `Catalog::Serialize(writer)`: write all schemas and tables to a `CheckpointWriter` (called by `CheckpointManager`)
- `Catalog::Deserialize(reader)`: restore the full catalog from a checkpoint block (called at startup)
- Maintain a reference to each table's `StorageEntry` (the list of `RowGroup`s for physical data)

## 3. Non-Responsibilities

- Does not store data rows (that is `RowGroup` / `ColumnChunk`)
- Does not manage the buffer pool
- Does not perform schema inference
- Does not enforce DDL permissions or roles
- Does not manage indexes (out of scope for this educational project)

## 4. Architecture Design

```
Catalog
  |
  +-- schemas_: unordered_map<string, Schema>
        |
        +-- Schema "main" (default)
        |     |
        |     +-- entries_: unordered_map<string, CatalogEntry*>
        |           |
        |           +-- TableCatalogEntry "orders"
        |           |     columns: [order_id INT64, customer_id INT64, amount FLOAT64]
        |           |     storage: StorageEntry (RowGroup list)
        |           |     create_tx_id, create_commit_time, delete_commit_time
        |           |
        |           +-- TableCatalogEntry "customers"
        |                 ...
        |
        +-- Schema "analytics"
              ...

MVCC visibility (GetEntry):
  entry.create_commit_time < tx.start_time  → entry visible
  entry.delete_commit_time < tx.start_time  → entry deleted (invisible)
  entry.create_tx_id == tx.tx_id            → created by this tx (visible)
  entry.delete_tx_id == tx.tx_id            → deleted by this tx (invisible)
```

## 5. Core Data Structures (C++)

```cpp
// src/catalog/catalog_entry.hpp
#pragma once
#include <string>
#include <vector>
#include "common/types.hpp"

namespace cppcoldb {

// Column definition stored in the catalog.
struct ColumnDefinition {
    std::string name;
    TypeId      type;
    bool        not_null    = false;
    bool        primary_key = false;
};

// Base class for all catalog entries (table, view, etc.).
struct CatalogEntry {
    enum class EntryType { TABLE };

    EntryType   entry_type;
    std::string name;
    std::string schema_name;

    // MVCC timestamps for this catalog entry.
    TransactionId  create_tx_id       = INVALID_TRANSACTION; // tx that created this
    timestamp_t    create_commit_time = INVALID_TIMESTAMP;   // 0 if uncommitted
    TransactionId  delete_tx_id       = INVALID_TRANSACTION; // tx that dropped this (0 if alive)
    timestamp_t    delete_commit_time = INVALID_TIMESTAMP;   // 0 if not dropped

    virtual ~CatalogEntry() = default;
};

// Physical storage reference for a table.
struct StorageEntry {
    std::vector<class RowGroup*> row_groups;  // non-owning pointers; owned by TableStorage
};

// A table entry in the catalog.
struct TableCatalogEntry : CatalogEntry {
    std::vector<ColumnDefinition> columns;
    StorageEntry                  storage;   // points to the table's RowGroups

    size_t ColumnCount() const { return columns.size(); }

    // Find column index by name; returns -1 if not found.
    int FindColumn(const std::string& col_name) const;

    std::vector<std::string> ColumnNames()  const;
    std::vector<TypeId>      ColumnTypes()  const;
};

} // namespace cppcoldb
```

```cpp
// src/catalog/schema.hpp
#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include "catalog/catalog_entry.hpp"

namespace cppcoldb {

class Transaction;

enum class OnNotFound { THROW, RETURN_NULL };

class Schema {
public:
    explicit Schema(std::string name);

    // MVCC-aware lookup. Returns nullptr if not found and policy=RETURN_NULL.
    // Throws BindError if not found and policy=THROW.
    CatalogEntry* GetEntry(const std::string& name,
                           const Transaction& tx,
                           OnNotFound policy = OnNotFound::THROW) const;

    void CreateEntry(std::unique_ptr<CatalogEntry> entry);
    void MarkDeleted(const std::string& name, const Transaction& tx);

    const std::string& Name() const { return name_; }

private:
    std::string name_;
    // All versions of entries (current + dropped) are kept until GC.
    std::unordered_map<std::string,
                       std::vector<std::unique_ptr<CatalogEntry>>> entries_;
};

} // namespace cppcoldb
```

```cpp
// src/catalog/catalog.hpp
#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include "catalog/schema.hpp"

namespace cppcoldb {

class CheckpointWriter;
class CheckpointReader;

class Catalog {
public:
    Catalog();

    // Schema management
    void     CreateSchema(const std::string& name, const Transaction& tx);
    void     DropSchema(const std::string& name, const Transaction& tx);
    Schema*  GetSchema(const std::string& name) const;

    // Entry lookup (MVCC-aware)
    CatalogEntry* GetEntry(const std::string& schema, const std::string& name,
                           const Transaction& tx,
                           OnNotFound policy = OnNotFound::THROW) const;

    TableCatalogEntry* GetTable(const std::string& schema,
                                const std::string& name,
                                const Transaction& tx) const;

    // DDL operations
    void CreateTable(const std::string& schema,
                     const std::string& name,
                     const std::vector<ColumnDefinition>& cols,
                     const Transaction& tx);
    void DropTable(const std::string& schema,
                   const std::string& name,
                   const Transaction& tx);

    // Commit / rollback DDL changes
    void CommitEntry(const std::string& schema, const std::string& name,
                     timestamp_t commit_time);
    void RollbackEntry(const std::string& schema, const std::string& name,
                       TransactionId tx_id);

    // Checkpoint serialization
    void Serialize(CheckpointWriter& writer) const;
    void Deserialize(CheckpointReader& reader, BufferManager& bm);

private:
    static constexpr char DEFAULT_SCHEMA[] = "main";

    mutable std::mutex                               mu_;
    std::unordered_map<std::string, std::unique_ptr<Schema>> schemas_;
};

} // namespace cppcoldb
```

## 6. Public Interfaces

```cpp
namespace cppcoldb {

class Catalog {
public:
    // Schema
    void    CreateSchema(const std::string& name, const Transaction& tx);
    void    DropSchema(const std::string& name, const Transaction& tx);
    Schema* GetSchema(const std::string& name) const;

    // Entry lookup
    CatalogEntry*      GetEntry(const std::string& schema,
                                const std::string& name,
                                const Transaction& tx,
                                OnNotFound policy = OnNotFound::THROW) const;
    TableCatalogEntry* GetTable(const std::string& schema,
                                const std::string& name,
                                const Transaction& tx) const;

    // DDL
    void CreateTable(const std::string& schema, const std::string& name,
                     const std::vector<ColumnDefinition>& cols,
                     const Transaction& tx);
    void DropTable(const std::string& schema, const std::string& name,
                   const Transaction& tx);

    // Catalog entry MVCC lifecycle
    void CommitEntry(const std::string& schema, const std::string& name,
                     timestamp_t commit_time);
    void RollbackEntry(const std::string& schema, const std::string& name,
                       TransactionId tx_id);

    // Persistence
    void Serialize(CheckpointWriter& writer) const;
    void Deserialize(CheckpointReader& reader, BufferManager& bm);
};

} // namespace cppcoldb
```

## 7. Internal Algorithms

### GetEntry (MVCC visibility check)
```
GetEntry(schema_name, entry_name, tx, policy):
  lock(mu_) [shared read lock]
  schema = schemas_[schema_name]
  if not schema:
    if policy == THROW: throw BindError("schema not found")
    return nullptr

  versions = schema.entries_[entry_name]
  // Find the most recent version visible to tx
  for entry in versions (newest first):
    if IsVisible(entry, tx): return entry

  if policy == THROW: throw BindError("table not found")
  return nullptr

IsVisible(entry, tx):
  // Created by this transaction:
  if entry.create_tx_id == tx.tx_id: goto check_delete

  // Not yet committed:
  if entry.create_commit_time == INVALID_TIMESTAMP: return false

  // Committed after this transaction started:
  if entry.create_commit_time >= tx.start_time: return false

check_delete:
  // Deleted by this transaction:
  if entry.delete_tx_id == tx.tx_id: return false

  // Not deleted:
  if entry.delete_commit_time == INVALID_TIMESTAMP: return true

  // Delete committed before this transaction started:
  if entry.delete_commit_time < tx.start_time: return false

  return true   // delete committed after T started → T still sees it
  Time: O(versions_per_entry) — usually O(1) for stable tables
```

### CreateTable
```
CreateTable(schema, name, cols, tx):
  lock(mu_) [write lock]
  s = GetSchema(schema)
  if existing = s.GetEntry(name, tx, RETURN_NULL): already exists → throw BindError
  entry = new TableCatalogEntry{name, schema, cols}
  entry.create_tx_id = tx.tx_id
  entry.create_commit_time = INVALID_TIMESTAMP  // uncommitted
  s.CreateEntry(entry)
  // Also add an UndoBuffer entry to the transaction for rollback
  Time: O(1)
```

### DropTable
```
DropTable(schema, name, tx):
  lock(mu_) [write lock]
  entry = GetEntry(schema, name, tx, THROW)
  entry.delete_tx_id = tx.tx_id
  entry.delete_commit_time = INVALID_TIMESTAMP  // pending
  // UndoBuffer entry records the delete for rollback
  Time: O(1)
```

## 8. Persistence Model

The Catalog is serialized to the `CATALOG_HEADER_BLOCK` of the `BlockFile` during every checkpoint. See `plan-checkpoint.md` for the binary format. At startup, `Catalog::Deserialize()` reads this block to reconstruct all schemas, tables, and `ColumnSegment` pointers.

Fields persisted per table entry:
- Schema name and table name
- Column definitions (name, type, not_null)
- Row group count and per-row-group column segment metadata (block_id, compression, row_count)

Fields **not** persisted (reconstructed at runtime):
- `create_tx_id`, `delete_tx_id` — cleared on checkpoint (all visible entries are fully committed)
- MVCC timestamps above the checkpoint horizon

## 9. Concurrency Model

| Object | Lock | Usage |
|--------|------|-------|
| `Catalog.mu_` | `std::mutex` (shared/exclusive via reader-writer) | Shared lock for GetEntry reads; exclusive lock for CreateTable/DropTable |

For simplicity in this educational project, a plain `std::mutex` is used (exclusive for all operations). A `std::shared_mutex` can be substituted for read-heavy workloads.

## 10. Configuration

```cpp
// Default schema name used when no schema is specified in SQL.
static constexpr char DEFAULT_SCHEMA[] = "main";
```

No runtime configuration. The catalog is fully determined by DDL statements.

## 11. Testing Strategy

- `TestCatalogCreateTable`: create table, GetEntry → returns correct TableCatalogEntry
- `TestCatalogTableNotFound`: GetEntry for non-existent table → throws BindError
- `TestCatalogDropTable`: create then drop → GetEntry after commit returns nullptr
- `TestCatalogMVCCCreateVisibility`: table created by T1, T2 started before T1 commits → T2 cannot see table
- `TestCatalogMVCCDropVisibility`: table dropped by T1, T2 started before T1 commits → T2 still sees table
- `TestCatalogRollbackCreate`: create table, rollback → table gone
- `TestCatalogRollbackDrop`: drop table, rollback → table still visible
- `TestCatalogCommitCreate`: create table, commit → new transaction can see table
- `TestCatalogSchemaNotFound`: GetEntry with unknown schema → throws BindError
- `TestCatalogCreateSchema`: CreateSchema "analytics", then CreateTable inside it → found via GetEntry
- `TestCatalogColumnLookup`: FindColumn by name → correct index; unknown name → -1
- `TestCatalogSerializeDeserialize`: create tables, serialize to buffer, deserialize → identical catalog

## 12. Open Questions

- Entry garbage collection: old versions of catalog entries (from dropped tables whose delete_commit_time is past all active transactions' start_time) accumulate in `Schema.entries_`. A GC pass triggered similarly to MVCC row GC would clean these up. Deferred.
