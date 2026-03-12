# Feature: Physical Planner

## 1. Purpose

The Physical Planner converts an optimized `LogicalPlan` into a `PhysicalPlan` — a tree of concrete physical operator objects that the Executor can run. Each logical node maps to one or more physical operators. The Physical Planner also decides which join algorithm to use (hash join vs. nested-loop) and how to organize operators into pipelines.

## 2. Responsibilities

- Accept an optimized `LogicalPlan` and return a `PhysicalPlan` (a tree of `PhysicalOperator` nodes)
- Map each logical operator type to its physical counterpart:
  - `LogicalGet` → `PhysicalTableScan`
  - `LogicalFilter` → `PhysicalFilter`
  - `LogicalProjection` → `PhysicalProjection`
  - `LogicalJoin` (equi) → `PhysicalHashJoinBuild` + `PhysicalHashJoinProbe`
  - `LogicalJoin` (non-equi or small right side) → `PhysicalNestedLoopJoin`
  - `LogicalAggregate` → `PhysicalHashAggregation`
  - `LogicalInsert` → `PhysicalInsert`
  - `LogicalDelete` → `PhysicalDelete`
  - `LogicalUpdate` → `PhysicalUpdate`
  - `LogicalCreateTable` → `PhysicalCreateTable`
  - `LogicalDropTable` → `PhysicalDropTable`
- Assign each physical operator its role: `SOURCE`, `OPERATOR`, or `SINK`
- Determine join algorithm selection: hash join if the join has an equality condition; nested-loop join otherwise
- Annotate each `PhysicalTableScan` with the pushed-down filter predicates from `LogicalGet.pushed_filters`

## 3. Non-Responsibilities

- Does not perform any further logical optimization
- Does not execute operators
- Does not access storage or catalog data
- Does not assign physical memory (buffer pool, hash table sizes)
- Does not build the `Pipeline` objects (that is the Executor's responsibility)

## 4. Architecture Design

```
Optimizer
  |
  v
LogicalPlan (optimized)
  |
  v
+-------------------+
|  PhysicalPlanner  |
|  Plan(logical)    |
|                   |
|  Recursive tree   |
|  walk, bottom-up  |
+-------------------+
  |
  v
PhysicalPlan
  (tree of PhysicalOperator nodes)
  |
  v
Executor::Initialize(physical_plan)
```

**Physical operator role assignment:**
```
SOURCE   — produces DataChunks from storage (PhysicalTableScan, ValuesSource)
OPERATOR — transforms an input DataChunk to an output DataChunk (Filter, Projection, HashJoinProbe)
SINK     — consumes DataChunks and accumulates state (ResultCollector, HashJoinBuild, HashAggSink)
```

## 5. Core Data Structures (C++)

```cpp
// src/execution/physical_operator.hpp
#pragma once
#include <memory>
#include <vector>
#include "common/types.hpp"
#include "planner/logical_plan/logical_plan.hpp"

namespace cppcoldb {

enum class OperatorRole { SOURCE, OPERATOR, SINK };

// Return codes from physical operator calls.
enum class OperatorResultType {
    HAVE_MORE_OUTPUT,  // chunk produced; call again
    NEEDS_MORE_INPUT,  // no output yet; provide next input chunk
    FINISHED,          // source exhausted or sink done
};

// Base class for all physical operators.
struct PhysicalOperator {
    OperatorRole role;
    std::vector<TypeId>      output_types;  // types of columns this operator emits
    std::vector<std::string> output_names;
    std::vector<std::unique_ptr<PhysicalOperator>> children;

    virtual ~PhysicalOperator() = default;
};

// ---- Source operators ----

struct PhysicalTableScan : PhysicalOperator {
    std::string          schema_name;
    std::string          table_name;
    std::vector<size_t>  column_ids;           // columns to read
    std::vector<std::unique_ptr<LogicalExpr>>  pushed_filters; // applied during scan
};

struct ValuesSource : PhysicalOperator {
    // Inline literal rows from INSERT ... VALUES (...)
    // Each inner vector is one row as a list of Value.
    std::vector<std::vector<Value>> rows;
};

// ---- Operator (transform) operators ----

struct PhysicalFilter : PhysicalOperator {
    std::unique_ptr<LogicalExpr> predicate;
};

struct PhysicalProjection : PhysicalOperator {
    std::vector<std::unique_ptr<LogicalExpr>> exprs;
};

struct PhysicalHashJoinProbe : PhysicalOperator {
    // References the corresponding build-side operator by shared ptr.
    // The Executor wires these together across pipelines.
    std::shared_ptr<struct PhysicalHashJoinBuild> build_side;
    std::vector<size_t> probe_key_cols;  // column indices in probe chunk
};

struct PhysicalNestedLoopJoin : PhysicalOperator {
    std::unique_ptr<LogicalExpr> condition; // nullptr for CROSS JOIN
};

struct PhysicalLimit : PhysicalOperator {
    int64_t limit;
    int64_t offset;
};

// ---- Sink operators ----

struct PhysicalHashJoinBuild : PhysicalOperator {
    std::vector<size_t> build_key_cols;  // column indices in build chunk
};

struct PhysicalHashAggregation : PhysicalOperator {
    std::vector<std::unique_ptr<LogicalExpr>>     group_keys;
    std::vector<std::unique_ptr<LogicalAggrExpr>> aggregates;
};

struct PhysicalInsert : PhysicalOperator {
    std::string         table_name;
    std::string         schema_name;
    std::vector<size_t> column_ids;
};

struct PhysicalDelete : PhysicalOperator {
    std::string table_name;
    std::string schema_name;
};

struct PhysicalUpdate : PhysicalOperator {
    std::string                               table_name;
    std::string                               schema_name;
    std::vector<size_t>                       column_ids;
    std::vector<std::unique_ptr<LogicalExpr>> new_values;
};

struct PhysicalCreateTable : PhysicalOperator {
    std::string              schema_name;
    std::string              table_name;
    std::vector<std::string> column_names;
    std::vector<TypeId>      column_types;
    std::vector<bool>        not_null_flags;
    bool                     if_not_exists;
};

struct PhysicalDropTable : PhysicalOperator {
    std::string schema_name;
    std::string table_name;
    bool        if_exists;
};

struct PhysicalResultCollector : PhysicalOperator {
    // Accumulates all output DataChunks for return to the client.
};

} // namespace cppcoldb
```

## 6. Public Interfaces

```cpp
// src/planner/physical_planner.hpp
namespace cppcoldb {

class PhysicalPlanner {
public:
    PhysicalPlanner() = default;

    // Convert a logical plan to a physical plan.
    // Returns the root physical operator.
    std::unique_ptr<PhysicalOperator> Plan(const LogicalPlan& logical);

private:
    std::unique_ptr<PhysicalOperator> PlanNode(const LogicalPlan& node);
    std::unique_ptr<PhysicalOperator> PlanGet(const LogicalGet& node);
    std::unique_ptr<PhysicalOperator> PlanFilter(const LogicalFilter& node);
    std::unique_ptr<PhysicalOperator> PlanProjection(const LogicalProjection& node);
    std::unique_ptr<PhysicalOperator> PlanJoin(const LogicalJoin& node);
    std::unique_ptr<PhysicalOperator> PlanAggregate(const LogicalAggregate& node);
    std::unique_ptr<PhysicalOperator> PlanInsert(const LogicalInsert& node);
    std::unique_ptr<PhysicalOperator> PlanDelete(const LogicalDelete& node);
    std::unique_ptr<PhysicalOperator> PlanUpdate(const LogicalUpdate& node);

    // Determine join algorithm based on join type and join condition.
    bool ShouldUseHashJoin(const LogicalJoin& node) const;
};

} // namespace cppcoldb
```

## 7. Internal Algorithms

### Plan(logical) — recursive tree walk
```
PlanNode(node):
  switch node.node_type:
    GET        -> PlanGet(node)
    FILTER     -> PlanFilter(node)
    PROJECTION -> PlanProjection(node)
    JOIN       -> PlanJoin(node)
    AGGREGATE  -> PlanAggregate(node)
    INSERT     -> PlanInsert(node)
    DELETE     -> PlanDelete(node)
    UPDATE     -> PlanUpdate(node)
    CREATE_TABLE -> PhysicalCreateTable{...}
    DROP_TABLE   -> PhysicalDropTable{...}
  // Each handler recursively calls PlanNode on children first (bottom-up)
  Time: O(plan_nodes)
```

### PlanJoin — algorithm selection
```
PlanJoin(LogicalJoin node):
  left_physical  = PlanNode(node.left)   // probe side
  right_physical = PlanNode(node.right)  // build side (post-reorder: smaller)

  if ShouldUseHashJoin(node):
    // Hash join: two separate physical operators linked by a shared hash table.
    build = PhysicalHashJoinBuild{
      role: SINK,
      child: right_physical,
      build_key_cols: ExtractKeyCols(node.condition, right_physical)
    }
    probe = PhysicalHashJoinProbe{
      role: OPERATOR,
      child: left_physical,
      build_side: shared_ptr(build),
      probe_key_cols: ExtractKeyCols(node.condition, left_physical)
    }
    return probe  // probe is the root; build is a separate pipeline

  else:
    // Nested-loop join: single operator with two children.
    nlj = PhysicalNestedLoopJoin{
      role: OPERATOR,
      children: [left_physical, right_physical],
      condition: node.condition
    }
    return nlj

ShouldUseHashJoin(node):
  return node.join_type != CROSS
         AND node.condition is not nullptr
         AND IsEquiJoinCondition(node.condition)
```

### PlanFilter wraps child
```
PlanFilter(LogicalFilter node):
  child_physical = PlanNode(node.children[0])
  return PhysicalFilter{
    role: OPERATOR,
    predicate: clone(node.predicate),
    children: [child_physical],
    output_types: child_physical.output_types
  }
```

## 8. Persistence Model

Not applicable. The Physical Planner is a pure in-memory transformation.

## 9. Concurrency Model

`PhysicalPlanner` is stateless and creates a fresh plan tree per call. No shared state; no locks needed.

## 10. Configuration

```cpp
struct PhysicalPlannerConfig {
    // Minimum right-side row count estimate to use hash join
    // (below this threshold, nested-loop may be faster due to hash overhead).
    uint64_t hash_join_min_build_rows = 16;
};
```

## 11. Testing Strategy

- `TestPlanTableScan`: LogicalGet → PhysicalTableScan with correct column_ids and pushed filters
- `TestPlanFilterWrapsChild`: LogicalFilter over LogicalGet → PhysicalFilter(PhysicalTableScan)
- `TestPlanProjectionExprsCopied`: LogicalProjection exprs are cloned correctly
- `TestPlanHashJoinEquiCondition`: equi-join → PhysicalHashJoinBuild + PhysicalHashJoinProbe
- `TestPlanNestedLoopJoinNonEqui`: non-equi join condition → PhysicalNestedLoopJoin
- `TestPlanCrossJoinNestedLoop`: CROSS JOIN (no condition) → PhysicalNestedLoopJoin
- `TestPlanHashAggregation`: LogicalAggregate → PhysicalHashAggregation with group keys and aggregates
- `TestPlanInsertValues`: LogicalInsert with VALUES → PhysicalInsert with ValuesSource child
- `TestPlanDeleteWrapsFilter`: DELETE with WHERE → PhysicalDelete with filter chain
- `TestPlanCreateTable`: LogicalCreateTable → PhysicalCreateTable, no children
- `TestPlanDropTable`: LogicalDropTable → PhysicalDropTable
- `TestPlanOutputTypesAnnotated`: verify each physical operator's output_types match parent expectations

## 12. Open Questions

- Handling of hash join when the build side does not fit in memory: for the educational scope this is not a concern; an out-of-core fallback can be added later.
- Sort operator: ORDER BY is currently not planned as a physical sort node; deferred.
