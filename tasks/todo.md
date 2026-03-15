# Phase 6 + 7 Implementation Plan

## Status Legend
- [ ] not started
- [~] in progress
- [x] done

---

## Step 1 — Shared expression + logical plan types
- [ ] `src/planner/logical_plan/logical_plan.hpp` — LogicalExpr hierarchy + all LogicalPlan nodes + CloneExpr decl
- [ ] `src/planner/logical_plan/logical_plan.cpp` — CloneExpr implementation

## Step 2 — BindContext
- [ ] `src/planner/bind_context.hpp`
- [ ] `src/planner/bind_context.cpp`

## Step 3 — Binder
- [ ] `src/planner/binder.hpp`
- [ ] `src/planner/binder.cpp` (BindSelect, BindInsert, BindCreate, BindDrop, BindUpdate, BindDelete, BindExpr, Coerce, ExpandStar)

## Step 4 — LogicalPlanner wrapper
- [ ] `src/planner/logical_planner.hpp`
- [ ] `src/planner/logical_planner.cpp`

## Step 5 — Optimizer
- [ ] `src/planner/optimizer.hpp`
- [ ] `src/planner/optimizer.cpp` (ConstantFold, PredicatePushdown, ColumnPruning, FilterMerge, JoinReordering stub)

## Step 6 — PhysicalPlanner
- [ ] `src/planner/physical_planner.hpp`
- [ ] `src/planner/physical_planner.cpp` (PlanGet, PlanFilter, PlanProjection, PlanLimit, PlanCreate, PlanDrop, column-idx remapping)

## Step 7 — ExprEvaluator + ClientContext
- [ ] `src/execution/operator/expr_evaluator.hpp`
- [ ] `src/execution/operator/expr_evaluator.cpp` (Evaluate, EvaluatePredicate, DataChunkCompact)
- [ ] `src/main/client_context.hpp` — add Catalog*, Transaction*

## Step 8 — Phase 6 operators
- [ ] `src/execution/operator/physical_table_scan.hpp`
- [ ] `src/execution/operator/physical_table_scan.cpp`
- [ ] `src/execution/operator/physical_filter.hpp`
- [ ] `src/execution/operator/physical_filter.cpp`
- [ ] `src/execution/operator/physical_projection.hpp`
- [ ] `src/execution/operator/physical_projection.cpp`
- [ ] `src/execution/operator/physical_limit.hpp`
- [ ] `src/execution/operator/physical_limit.cpp`

## Step 9 — DDL operators (needed by planner)
- [ ] `src/execution/operator/physical_create_table.hpp`
- [ ] `src/execution/operator/physical_create_table.cpp`
- [ ] `src/execution/operator/physical_drop_table.hpp`
- [ ] `src/execution/operator/physical_drop_table.cpp`

## Step 10 — CMakeLists.txt
- [ ] Add test_phase6 and test_phase7 targets

## Step 11 — Phase 6 tests
- [ ] `test/execution/test_phase6_main.cpp`
- [ ] `test/execution/test_table_scan.cpp`
- [ ] `test/execution/test_filter.cpp`
- [ ] `test/execution/test_projection.cpp`
- [ ] `test/execution/test_limit.cpp`

## Step 12 — Phase 7 tests
- [ ] `test/planner/test_phase7_main.cpp`
- [ ] `test/planner/test_binder.cpp`
- [ ] `test/planner/test_optimizer.cpp`
- [ ] `test/planner/test_physical_planner.cpp`

---

## Key Design Decisions (locked)
- `LogicalExpr` in `src/planner/logical_plan/logical_plan.hpp` — single bound-expression type for both phases
- `LogicalLit` unifies all literal types using `Value`
- Column index remapping at `PhysicalPlanner` time (BoundColumnRef::column_idx rewritten to chunk-position)
- `PhysicalFilter::Execute` returns `NEED_MORE_INPUT` (not `HAVE_MORE_OUTPUT`)
- `ClientContext` stays default-constructible (nullptr fields) — Phase 5 tests unaffected
- `pushed_filters` on `LogicalGet` are zone-map hints only; runtime filtering done by `PhysicalFilter`

## Critical Traps
1. Column index remapping: binder uses table-global idx; chunk uses pruned-list position
2. `RowGroup::Scan` updates `row_offset` in-place — do not double-increment
3. `DataChunkSlice` takes `uint32_t`; `EvaluatePredicate` returns `uint16_t` — convert in `DataChunkCompact`
4. Predicate pushdown must return replacement node from `Pushdown()` (not mutate in-place)
5. Column pruning root: if top node is not `LogicalProjection`, required_cols starts empty → keep all columns

---

## Review
_to be filled after implementation_
