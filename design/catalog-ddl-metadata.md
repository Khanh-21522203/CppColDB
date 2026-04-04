# Catalog and DDL Metadata

## Purpose

Maintain MVCC-visible schema/table metadata, perform DDL mutations, and serialize/deserialize catalog/table segment metadata for checkpoints.

## Scope

**In scope:**
- Schema and table metadata storage (`Catalog`, `Schema`, `TableCatalogEntry`).
- DDL APIs (`CreateTable`, `DropTable`, `DropPartition`, `AddPartition`).
- Catalog MVCC lifecycle hooks (`CommitEntry`, `RollbackCreate`, `RollbackDrop`).
- Checkpoint metadata serialization/deserialization.

**Out of scope:**
- SQL parse/bind of DDL statements.
- WAL append/replay and checkpoint orchestration logic.

## Primary User Flow

1. DDL physical operators call catalog methods inside a transaction.
2. `Catalog` creates or marks entries deleted (MVCC markers on catalog entries).
3. Transaction commit calls `Catalog::CommitEntry` to stamp commit visibility.
4. Checkpoint path calls `Catalog::FlushAllRowGroups`, `Serialize`, and later startup calls `Deserialize`.

## System Flow

1. `Catalog::CreateTable` validates schema/existence and inserts a new `TableCatalogEntry` version with `create_tx_id` set.
2. `Catalog::DropTable` marks the visible entry deleted for the transaction (`delete_tx_id`).
3. Partition DDL methods mutate `TableCatalogEntry.partition_info` and `partition_rg_indices`, returning old state for undo.
4. Commit/rollback hooks delegate into `Schema` methods to set commit times or clear pending versions.
5. `Serialize` writes committed, non-deleted tables and row-group segment metadata into checkpoint block 0 format.
6. `Deserialize` rebuilds schemas/tables/row-groups from checkpoint bytes and restores partition metadata/index mappings.

```
DDL operator
  -> Catalog::{CreateTable|DropTable|AddPartition|DropPartition}
  -> UndoBuffer records catalog/partition change
  -> TransactionManager commit/rollback
     -> Catalog::{CommitEntry|RollbackCreate|RollbackDrop|RestorePartitionState}

Checkpoint
  -> Catalog::FlushAllRowGroups
  -> Catalog::Serialize(block0)
  -> restart: Catalog::Deserialize(block0)
```

## Data Model

- `CatalogEntry` (`src/catalog/catalog_entry.hpp`):
- identity: `name`, `schema_name`, `entry_type`
- MVCC fields: `create_tx_id`, `create_commit_time`, `delete_tx_id`, `delete_commit_time`
- `TableCatalogEntry`:
- `columns (std::vector<ColumnDefinition>)`
- `row_groups (std::vector<std::unique_ptr<RowGroup>>)`
- `partition_info (PartitionInfo)`
- `partition_rg_indices (std::vector<std::vector<size_t>>)`
- `ColumnDefinition` fields: `name`, `type`, `not_null`, `primary_key`
- `Schema` stores version chains: `unordered_map<string, vector<unique_ptr<CatalogEntry>>>`

## Interfaces and Contracts

- `Catalog::GetEntry/GetTable` are MVCC-aware by transaction snapshot.
- `Catalog::CreateTable` throws when a visible table version already exists.
- `Catalog::DropTable` marks visible entry deleted; physical removal is version-based visibility.
- `Catalog::DropPartition/AddPartition` capture old state outputs for undo logging.
- `Catalog::Serialize(uint8_t* block, size_t block_size)` writes committed visible metadata only.
- `Catalog::Deserialize(const uint8_t* block, size_t block_size)` rebuilds table entries and segment metadata.

## Dependencies

**Internal modules:**
- `src/storage/column/row_group.hpp` and segment metadata for checkpoint serialization.
- `src/transaction/transaction.hpp` for snapshot visibility checks.
- `src/storage/partition_info.hpp` for partition metadata.

**External services/libraries:**
- None directly; block I/O is done by checkpoint/buffer modules.

## Failure Modes and Edge Cases

- Schema lookup errors throw `BindError` when schema/table is missing.
- `Schema::Resolve` behavior returns newest visible version; uncommitted/invisible versions are skipped.
- `DropPartition`/`AddPartition` operate only on already partitioned tables; validation mostly happens in binder/operator layer.
- `Deserialize` reconstructs entries with commit time `0` (`INVALID_TRANSACTION` owner) to make checkpoint state globally visible.

## Observability and Debugging

- Debugging entry points:
- visibility issues: `src/catalog/schema.cpp:IsCatalogEntryVisible`
- DDL metadata mutation: `src/catalog/catalog.cpp`
- checkpoint payload shape: `Catalog::Serialize` / `Catalog::Deserialize`
- No built-in catalog logging or metrics.

## Risks and Notes

- Checkpoint serialization/deserialization does not enforce explicit `block_size` bounds checks while writing/reading, so oversized metadata can corrupt block payload.
- `Catalog::FlushAllRowGroups` skips row groups with uncommitted version markers, which couples checkpoint eligibility to MVCC cleanup state.

Changes:

