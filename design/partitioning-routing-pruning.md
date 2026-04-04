# Table Partitioning, Routing, and Pruning

## Purpose

Implement RANGE/HASH/LIST partition metadata, route inserted rows to partitions, and prune partitions during scans and compatible joins.

## Scope

**In scope:**
- Partition metadata structures (`PartitionInfo`, `PartitionDef`).
- Routing logic for insert/replay paths.
- Predicate-based partition pruning.
- Partition DDL (`ALTER TABLE ... DROP/ADD PARTITION`).
- Partition-aware scan and partition-wise hash join path.

**Out of scope:**
- Generic non-partitioned table scan logic.
- Compression and block persistence internals.

## Primary User Flow

1. User defines partitioned table in `CREATE TABLE ... PARTITION BY ...`.
2. Binder resolves partition key columns and materializes `PartitionInfo` in logical create node.
3. Create-table operator stores partition metadata in catalog entry.
4. Insert path routes each row by partition key and appends into partition row-group list.
5. Scan path prunes partitions from pushed predicates and reads only active partition row groups.

## System Flow

1. Parse stage captures partition clause fields in `CreateTableStatement`.
2. `Binder::BindCreateTable` resolves `partition_col_idxs`, validates bounds/definitions, and builds `PartitionInfo`.
3. `Catalog::CreateTable` stores `partition_info`; initializes `partition_rg_indices` for partitioned tables.
4. `PhysicalInsert` / WAL replay path computes key tuple and calls `PartitionInfo::RouteRow`.
5. `PhysicalTableScan::InitScan` builds table-column predicates then calls `PartitionInfo::PrunedPartitions`.
6. For join plans with compatible partition metadata and key mapping, `PhysicalPlanner::PlanJoin` enables per-partition hash tables.

```
CREATE TABLE ... PARTITION BY ...
  -> parser AST partition fields
  -> binder PartitionInfo
  -> catalog table entry
INSERT
  -> RouteRow(keys) -> partition id
SCAN
  -> PrunedPartitions(predicates) -> active partition ids
```

## Data Model

- `PartitionType` (`src/storage/partition_info.hpp`): `NONE`, `RANGE`, `HASH`, `LIST`.
- `PartitionDef`:
- RANGE: `upper_bounds (vector<Value>)`
- LIST: `list_values (vector<vector<Value>>)`
- `PartitionInfo` fields:
- `type`
- `partition_cols (vector<string>)`
- `partition_col_idxs (vector<int>)`
- `num_partitions (uint32_t)`
- `defs (vector<PartitionDef>)`
- Table linkage (`src/catalog/catalog_entry.hpp`):
- `partition_info`
- `partition_rg_indices[pid] -> vector<row_group_index>`

## Interfaces and Contracts

- `uint32_t PartitionInfo::RouteRow(const Value* keys, size_t nkeys) const`
- contract: partition keys must be non-null, throws on LIST miss.
- `std::vector<uint32_t> PartitionInfo::PrunedPartitions(const vector<ScanPredicate>&)`
- contract: predicates use table-column indices; pruning primarily uses leading partition key.
- `Catalog::DropPartition/AddPartition/RestorePartitionState`
- contract: mutate or restore partition metadata and row-group mapping.
- `PhysicalAlterTable::GetData`
- contract: applies partition change and pushes undo entry with old/new state snapshots.

## Dependencies

**Internal modules:**
- `src/parser/parser.cpp` partition grammar.
- `src/planner/binder.cpp` partition validation and value conversion.
- `src/execution/operator/physical_insert.cpp` and `src/main/database.cpp` WAL replay route logic.
- `src/execution/operator/physical_table_scan.cpp` pruning and partitioned scan path.

**External services/libraries:**
- None.

## Failure Modes and Edge Cases

- Partition keys cannot be NULL; inserts throw `RuntimeError`.
- Insert/replay routing uses stack arrays capped at 8 keys (`Value keys[8]`), limiting practical multi-key support.
- HASH composite pruning requires equality predicates on all key columns; otherwise all partitions stay active.
- LIST routing throws if value tuple matches no partition definition.
- RANGE pruning is leading-key-focused; composite bounds are conservative and may keep extra partitions.

## Observability and Debugging

- `EXPLAIN` output includes partition type, key columns, and partition count for scans (`ClientContext::FormatLogicalPlan`).
- Partition functional tests: `test/partition/test_partition.cpp`.
- Debug entry points:
- routing/pruning logic: `src/storage/partition_info.cpp`
- scan behavior: `src/execution/operator/physical_table_scan.cpp`

## Risks and Notes

- RANGE boundary semantics rely on lexicographic comparisons over `Value`; comments and equality behavior should be validated carefully when adjusting logic.
- Empty active partition sets can interact badly with scan fallback behavior; callers assume pruning result semantics are correct.

Changes:

