# Query Planning (Bind, Optimize, Physical Plan)

## Purpose

Transform parsed SQL AST into executable physical operator trees with validated names/types and basic rule-based optimizations.

## Scope

**In scope:**
- Binding AST to logical expressions/plans (`Binder`).
- Name resolution (`BindContext`).
- Logical optimization passes (`Optimizer`).
- Logical-to-physical lowering (`PhysicalPlanner`).

**Out of scope:**
- Tokenization and parsing.
- Runtime execution behavior in pipelines.

## Primary User Flow

1. `ClientContext::Query` creates `Binder` and calls `Bind(*stmt)`.
2. `Optimizer::Optimize` runs constant folding, predicate pushdown, column pruning, filter merge.
3. `PhysicalPlanner::Plan` produces a physical operator tree (with shared state nodes for sort/agg/join patterns).

## System Flow

1. `Binder::Bind` dispatches by statement type (`SELECT/INSERT/UPDATE/DELETE/CREATE_TABLE/DROP_TABLE/ALTER_TABLE`).
2. `BindSelect` builds `LogicalGet`, optional `LogicalFilter`, optional `LogicalSort`, `LogicalProjection`, optional `LogicalLimit`.
3. Aggregation path uses `BindAggregateSelect` to build `LogicalAggregate` then projection over aggregate outputs.
4. Join path (`BindSelectWithJoin`) builds chained `LogicalJoin` nodes and extracts equi-join key positions.
5. `Optimizer::Optimize` applies enabled passes unless plan is DDL/DML trivial.
6. `PhysicalPlanner::PlanNode` maps each logical node to concrete physical operators, including two-pipeline SINK+SOURCE shapes for sort and hash aggregation.

```
ParsedStatement
  -> Binder::Bind
     -> LogicalPlan tree + typed LogicalExpr nodes
  -> Optimizer::Optimize
     -> rewritten LogicalPlan
  -> PhysicalPlanner::Plan
     -> PhysicalOperator tree (scan/filter/projection/limit/sort/join/agg/dml/ddl)
```

## Data Model

- `ColumnBinding` (`src/planner/bind_context.hpp`): `table_idx`, `column_idx`, `type`, `column_name`, `table_name`.
- Logical expression hierarchy (`src/planner/logical_plan/logical_plan.hpp`):
- `BoundColumnRef`, `LogicalLit`, `LogicalBinaryOp`, `LogicalUnaryOp`, `LogicalCast`, `LogicalAggrExpr`
- Logical plan nodes:
- relational: `LogicalGet`, `LogicalFilter`, `LogicalProjection`, `LogicalLimit`, `LogicalSort`, `LogicalAggregate`, `LogicalJoin`
- mutations/DDL: `LogicalInsert`, `LogicalDelete`, `LogicalUpdate`, `LogicalCreateTable`, `LogicalDropTable`, `LogicalAlterTable`
- Planner metadata:
- `LogicalGet.column_ids` (table-column ids to scan)
- `LogicalGet.pushed_filters` (zone-map/scan hints)
- `LogicalJoin.left_key_col_ids/right_key_col_ids` (equi-key positions)

## Interfaces and Contracts

- `std::unique_ptr<LogicalPlan> Binder::Bind(const ParsedStatement&)` (`src/planner/binder.cpp`)
- contract: throws `BindError` for missing/ambiguous columns, invalid types, unsupported forms.
- `std::unique_ptr<LogicalPlan> Optimizer::Optimize(std::unique_ptr<LogicalPlan>)`
- contract: preserves schema fields (`output_types`, `output_names`) while rewriting.
- `std::unique_ptr<PhysicalOperator> PhysicalPlanner::Plan(const LogicalPlan&)`
- contract: returns executable tree using operator roles and dependency patterns expected by `Executor`.

## Dependencies

**Internal modules:**
- `src/catalog` and `src/transaction` for table lookup and MVCC-aware metadata visibility during binding.
- `src/execution/operator/*` for physical operator classes.
- `src/storage/partition_info.hpp` for partition metadata/pruning hints in logical scans.

**External services/libraries:**
- None.

## Failure Modes and Edge Cases

- `BindContext::ResolveColumn` throws on ambiguous unqualified names.
- Aggregate rules are intentionally narrow:
- any function call is treated as aggregate candidate (`HasAggregateExpr`)
- complex non-column expressions in grouped SELECT are rejected (`BindAggregateSelect`).
- Join key extraction only captures equi-keys (`col = col`, possibly AND-combined); non-equi ON logic is not used for hash keying.
- `INSERT VALUES` requires literals only; non-literal expressions raise `BindError`.
- `ALTER TABLE ADD PARTITION` rejects HASH partition extensions.
- `Optimizer::IsTrivialPlan` skips optimization for create/drop/insert/update/delete plans.

## Observability and Debugging

- `EXPLAIN` formatting in `ClientContext::Query` traverses logical plan nodes (`FormatLogicalPlan`) and includes partition metadata on scans.
- Debugging entry points:
- semantic/type errors: `src/planner/binder.cpp`
- rewrite behavior: `src/planner/optimizer.cpp`
- operator mapping/top-k hinting: `src/planner/physical_planner.cpp`

## Risks and Notes

- `SELECT DISTINCT` is parsed but there is no distinct-specific logical node/planner lowering.
- Join ON expression is stored (`LogicalJoin.condition`) but runtime hash join primarily uses extracted key columns.
- LIMIT over ORDER BY uses top-k hints (`PhysicalPlanner::PlanLimit` + `PlanSort`) to reduce sort work.

Changes:

