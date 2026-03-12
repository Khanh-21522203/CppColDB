# Optimizer Pipeline Flow

## Assumptions
- The optimizer operates on the LogicalPlan and returns a transformed LogicalPlan.
- Passes are applied in a fixed order; each pass is a separate optimizer rule.
- The optimizer is intentionally simple — it covers the most impactful optimizations without complexity.
- Individual passes can be disabled for debugging.

## Diagram

```mermaid
flowchart TD
    A["Optimizer::Optimize(logical_plan)"] --> B{"Plan is trivial?\n(e.g. COMMIT/ROLLBACK)"}
    B -- Yes --> SKIP["Skip all passes\nReturn plan unchanged"]
    B -- No --> C["Run optimization passes"]

    C --> P1["1. CONSTANT_FOLDING\nEvaluate constant expressions at plan time\ne.g. 1+2 → 3, 'a'='a' → true"]
    P1 --> P2["2. PREDICATE_PUSHDOWN\nPush filter nodes closer to scan sources\nReduces rows processed early"]
    P2 --> P3["3. COLUMN_PRUNING\nRemove columns not needed by parent nodes\nReduces data read from storage"]
    P3 --> P4["4. FILTER_MERGE\nCombine adjacent filter nodes\ninto a single combined predicate"]
    P4 --> P5["5. JOIN_REORDERING\nReorder joins using estimated cardinality\nSmaller tables as inner (build) side"]

    P5 --> Q["Return optimized LogicalPlan"]
    SKIP --> Q

    subgraph PassDetail["Pass Details"]
        PP1["CONSTANT_FOLDING:\nWalk expression tree\nFold literals, simplify boolean logic"]
        PP2["PREDICATE_PUSHDOWN:\nMove LogicalFilter below LogicalJoin\nPush into LogicalGet where possible"]
        PP3["COLUMN_PRUNING:\nTrace which columns are consumed\nDrop unused columns from LogicalGet"]
        PP4["FILTER_MERGE:\nCollect consecutive filter predicates\nAND them into one LogicalFilter node"]
        PP5["JOIN_REORDERING:\nUse table row-count estimates\nReorder join tree to minimize intermediate size"]
    end
```

## Planned Implementation
- `src/optimizer/optimizer.cpp` — Optimizer::Optimize(), pass dispatch
- `src/optimizer/constant_folding.cpp` — constant folding pass
- `src/optimizer/predicate_pushdown.cpp` — predicate pushdown pass
- `src/optimizer/column_pruning.cpp` — column pruning pass
- `src/optimizer/filter_merge.cpp` — filter merge pass
- `src/optimizer/join_reordering.cpp` — join reordering pass
