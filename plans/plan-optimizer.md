# Feature: Optimizer

## 1. Purpose

The Optimizer rewrites a `LogicalPlan` into a semantically equivalent but more efficient `LogicalPlan` before physical planning. It applies a fixed sequence of rule-based passes — constant folding, predicate pushdown, column pruning, filter merge, and join reordering. The optimizer is intentionally simple and educational: it covers the most impactful optimizations without the complexity of a cost-based search framework.

## 2. Responsibilities

- Accept a `LogicalPlan` from the Binder and return a transformed `LogicalPlan`
- Apply passes in a fixed order: constant folding → predicate pushdown → column pruning → filter merge → join reordering
- Allow individual passes to be disabled for debugging via a `OptimizerConfig` bitmask
- Skip all passes for trivial plans (COMMIT, ROLLBACK, CREATE TABLE, DROP TABLE) that have no scan nodes
- Constant folding: evaluate constant sub-expressions at plan time (e.g., `1+2` → `3`, `'a'='a'` → `true`)
- Predicate pushdown: move `LogicalFilter` nodes as close as possible to `LogicalGet` (scan) nodes to reduce early row count
- Column pruning: trace which columns are actually consumed by parent nodes and remove the rest from `LogicalGet.column_ids`
- Filter merge: combine consecutive `LogicalFilter` nodes into a single node with an AND predicate
- Join reordering: reorder join operands using table row-count estimates so the smaller table becomes the build side

## 3. Non-Responsibilities

- Does not perform cost estimation beyond simple row-count heuristics
- Does not rewrite physical operator types (that is the PhysicalPlanner's job)
- Does not access storage or the Catalog during optimization (row counts are cached in the plan)
- Does not introduce new operator types (e.g., sort-merge join — only selects build/probe sides)
- Does not handle subquery decorrelation

## 4. Architecture Design

```
Binder
  |
  v
LogicalPlan (unoptimized)
  |
  v
+------------------------------------------------------------+
|                      Optimizer                             |
|                                                            |
|  Pass 1: ConstantFolding      (expression tree rewrite)   |
|  Pass 2: PredicatePushdown    (filter movement)           |
|  Pass 3: ColumnPruning        (reduce LogicalGet cols)    |
|  Pass 4: FilterMerge          (AND-combine filters)       |
|  Pass 5: JoinReordering       (swap build/probe sides)    |
+------------------------------------------------------------+
  |
  v
LogicalPlan (optimized)
  |
  v
PhysicalPlanner
```

**Pass ordering rationale:**
- Constant folding first: reduces predicates before pushing them down
- Predicate pushdown second: filters travel toward scans while expressions are already simplified
- Column pruning third: after filter pushdown we know the final projected columns
- Filter merge fourth: clean up any adjacent filters left by pushdown
- Join reordering last: uses column-pruned row estimates for accurate sizing

## 5. Core Data Structures (C++)

```cpp
// src/optimizer/optimizer.hpp
#pragma once
#include <cstdint>
#include <memory>
#include "planner/logical_plan/logical_plan.hpp"

namespace cppcoldb {

// Bitmask for enabling/disabling individual optimization passes.
enum class OptimizerPassFlag : uint32_t {
    NONE              = 0,
    CONSTANT_FOLDING  = 1 << 0,
    PREDICATE_PUSHDOWN= 1 << 1,
    COLUMN_PRUNING    = 1 << 2,
    FILTER_MERGE      = 1 << 3,
    JOIN_REORDERING   = 1 << 4,
    ALL               = 0xFFFFFFFF,
};
inline OptimizerPassFlag operator|(OptimizerPassFlag a, OptimizerPassFlag b) {
    return static_cast<OptimizerPassFlag>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

struct OptimizerConfig {
    OptimizerPassFlag enabled_passes = OptimizerPassFlag::ALL;
};

class Optimizer {
public:
    explicit Optimizer(OptimizerConfig cfg = {});

    // Apply all enabled passes in order.
    std::unique_ptr<LogicalPlan> Optimize(std::unique_ptr<LogicalPlan> plan);

private:
    std::unique_ptr<LogicalPlan> RunConstantFolding(std::unique_ptr<LogicalPlan> plan);
    std::unique_ptr<LogicalPlan> RunPredicatePushdown(std::unique_ptr<LogicalPlan> plan);
    std::unique_ptr<LogicalPlan> RunColumnPruning(std::unique_ptr<LogicalPlan> plan,
                                                  std::vector<size_t> required_cols);
    std::unique_ptr<LogicalPlan> RunFilterMerge(std::unique_ptr<LogicalPlan> plan);
    std::unique_ptr<LogicalPlan> RunJoinReordering(std::unique_ptr<LogicalPlan> plan);

    // Check whether a plan node is trivial (no optimization needed).
    static bool IsTrivialPlan(const LogicalPlan& plan);

    OptimizerConfig cfg_;
};

} // namespace cppcoldb
```

## 6. Public Interfaces

```cpp
namespace cppcoldb {

class Optimizer {
public:
    explicit Optimizer(OptimizerConfig cfg = {});

    // Entry point: apply all enabled passes.
    // Returns the optimized plan (may be the same tree with mutations,
    // or a newly constructed replacement).
    std::unique_ptr<LogicalPlan> Optimize(std::unique_ptr<LogicalPlan> plan);
};

} // namespace cppcoldb
```

## 7. Internal Algorithms

### Pass 1: Constant Folding
```
FoldExpr(expr):
  if expr is BinaryOp:
    left  = FoldExpr(expr.left)
    right = FoldExpr(expr.right)
    if left is Literal AND right is Literal:
      return EvalBinaryOp(expr.op, left, right)  // O(1) arithmetic/comparison
    expr.left  = left
    expr.right = right
    return expr
  if expr is UnaryOp:
    child = FoldExpr(expr.child)
    if child is Literal:
      return EvalUnaryOp(expr.op, child)
    return expr
  return expr  // non-foldable leaf

FoldPlan(node):
  for child in node.children:
    FoldPlan(child)
  if node is LogicalFilter:
    node.predicate = FoldExpr(node.predicate)
    if node.predicate is BoolLit(true):
      replace node with node.child  // filter always passes
  Time: O(plan_nodes * expr_depth)
```

### Pass 2: Predicate Pushdown
```
Pushdown(node, pending_predicates=[]):
  if node is LogicalFilter:
    // Collect this filter's predicate and try to push it further down
    pending_predicates.append(node.predicate)
    return Pushdown(node.child, pending_predicates)

  if node is LogicalGet:
    // Apply all pending predicates that only reference this table's columns
    applicable, remaining = Partition(pending_predicates,
      lambda p: ReferencedTables(p) == {node.table_name})
    node.pushed_filters.extend(applicable)
    if remaining is non-empty:
      // Wrap node in LogicalFilter for predicates that could not be pushed
      return LogicalFilter(AND(remaining), [node])
    return node

  if node is LogicalJoin:
    // Push predicates into left or right child if they only reference one side
    left_preds  = [p for p in pending_predicates if OnlyRefsLeft(p, node)]
    right_preds = [p for p in pending_predicates if OnlyRefsRight(p, node)]
    rest        = remaining predicates
    node.left  = Pushdown(node.left,  left_preds)
    node.right = Pushdown(node.right, right_preds)
    if rest: wrap node in LogicalFilter(AND(rest))
    return node

  // For other nodes: recurse into children without pending predicates
  node.children = [Pushdown(c, []) for c in node.children]
  return node
  Time: O(plan_nodes * predicates)
```

### Pass 3: Column Pruning
```
Prune(node, required_col_set):
  if node is LogicalGet:
    node.column_ids = intersection(node.column_ids, required_col_set)
    return node

  if node is LogicalProjection:
    cols_used = Union(ColumnsInExpr(e) for e in node.exprs)
    node.child = Prune(node.child, cols_used)
    return node

  if node is LogicalFilter:
    cols_used = ColumnsInExpr(node.predicate) ∪ required_col_set
    node.child = Prune(node.child, cols_used)
    return node

  // Default: pass required_col_set to children unchanged
  node.children = [Prune(c, required_col_set) for c in node.children]
  return node
  Time: O(plan_nodes * cols)
```

### Pass 4: Filter Merge
```
MergeFilters(node):
  node.children = [MergeFilters(c) for c in node.children]
  if node is LogicalFilter AND node.child is LogicalFilter:
    combined = BinaryOp("AND", node.predicate, node.child.predicate)
    merged = LogicalFilter(combined)
    merged.child = node.child.child
    return merged
  return node
  Time: O(plan_nodes)
```

### Pass 5: Join Reordering
```
ReorderJoin(node):
  if node is not LogicalJoin: return node
  node.left  = ReorderJoin(node.left)
  node.right = ReorderJoin(node.right)

  left_rows  = EstimateRows(node.left)   // from LogicalGet.catalog_row_count
  right_rows = EstimateRows(node.right)

  // Smaller side = build side (right child in hash join convention).
  if left_rows < right_rows:
    swap(node.left, node.right)
  return node

EstimateRows(node):
  if node is LogicalGet:
    return node.catalog_row_count  // stored from catalog at bind time
  if node is LogicalFilter:
    return max(1, EstimateRows(node.child) / 10)  // rough 10% selectivity
  return EstimateRows(node.children[0])
  Time: O(join_nodes)
```

## 8. Persistence Model

Not applicable. The Optimizer is a pure in-memory transformation pass.

## 9. Concurrency Model

`Optimizer` is created per-query and is not shared across threads. No locks needed.

## 10. Configuration

```cpp
struct OptimizerConfig {
    // Bitmask of OptimizerPassFlag values.
    // Default: all passes enabled.
    OptimizerPassFlag enabled_passes = OptimizerPassFlag::ALL;

    // Cardinality threshold below which join reordering skips a join
    // (both sides are small, reordering doesn't matter).
    uint64_t join_reorder_min_rows = 100;
};
```

## 11. Testing Strategy

- `TestConstantFoldArithmetic`: plan with `1 + 2 * 3` in projection → folded to `7`
- `TestConstantFoldBoolTrue`: `WHERE 1 = 1` → LogicalFilter removed (always true)
- `TestConstantFoldBoolFalse`: `WHERE 1 = 2` → plan replaced with empty result
- `TestPredicatePushdownSingleTable`: `SELECT * FROM t WHERE a > 5` → filter pushed into LogicalGet
- `TestPredicatePushdownJoin`: join with independent predicates → each pushed to respective child
- `TestColumnPruningSelect`: `SELECT a FROM t` → LogicalGet.column_ids contains only col a's index
- `TestColumnPruningFilter`: filter on col b, select col a → LogicalGet reads both a and b
- `TestFilterMergeTwoFilters`: two consecutive LogicalFilter nodes → merged into one with AND
- `TestJoinReorderSmallBuildSide`: left=1000 rows, right=10 rows → right stays as right (build side)
- `TestJoinReorderSwap`: left=10 rows, right=1000 rows → sides swapped
- `TestTrivialPlanSkipped`: `COMMIT` statement → all passes skipped, plan returned unchanged
- `TestPassDisableFlag`: disable COLUMN_PRUNING, verify LogicalGet still has all columns
- `TestAllPassesComposed`: complex query exercises all 5 passes together, verify plan structure

## 12. Open Questions

- Statistics-based selectivity estimation: the current 10% heuristic for filter cardinality is crude; a future iteration could store column histograms in the catalog.
- Multi-join reordering beyond pairwise swap: for queries with 3+ join nodes, a proper dynamic-programming join order search may be added later.
