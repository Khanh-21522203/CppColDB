# Aggregation Flow

## Assumptions
- CppColDB uses hash aggregation for grouped aggregates (GROUP BY).
- Ungrouped aggregates (no GROUP BY) are a degenerate case handled by the same operator with a single group.
- Aggregate state is stored per group key in an AggregateHashTable.
- After all input is consumed, the hash table is iterated to finalize and emit result chunks.

## Diagram

```mermaid
flowchart TD
    A["LogicalAggregate in plan"] --> B["PhysicalPlanner creates\nPhysicalHashAggregation operator"]

    B --> C{"Has GROUP BY\nclauses?"}
    C -- Yes --> D["Grouped aggregation\n(hash each group key)"]
    C -- No --> E["Ungrouped aggregation\n(single implicit group)"]

    subgraph BuildPhase["Aggregation Build Phase: Consume Input"]
        BP1["HashAggSink::Consume(input_chunk)"]
        BP1 --> BP2["For each row in input_chunk"]
        BP2 --> BP3{"Grouped?"}
        BP3 -- Yes --> BP4["Compute hash of GROUP BY\ncolumn values for this row"]
        BP3 -- No --> BP5["Use fixed group_id = 0"]
        BP4 --> BP6["Look up group in AggregateHashTable"]
        BP5 --> BP6
        BP6 --> BP7{"Group\nalready exists?"}
        BP7 -- No --> BP8["Create new AggregateState\nfor this group\n(zero-initialize accumulators)"]
        BP8 --> BP9["Update aggregate accumulators"]
        BP7 -- Yes --> BP9
        BP9 --> BP10{"Accumulator type?"}
        BP10 -- COUNT --> BP11["state.count += 1"]
        BP10 -- SUM --> BP12["state.sum += row_value"]
        BP10 -- MIN --> BP13["state.min = min(state.min, row_value)"]
        BP10 -- MAX --> BP14["state.max = max(state.max, row_value)"]
        BP10 -- AVG --> BP15["state.sum += row_value\nstate.count += 1"]
        BP11 --> BP16["Continue to next row"]
        BP12 --> BP16
        BP13 --> BP16
        BP14 --> BP16
        BP15 --> BP16
        BP16 --> BP2
    end

    subgraph FinalizePhase["Aggregation Finalize Phase: Emit Results"]
        FP1["HashAggSink::Finalize()\ncalled after all input consumed"]
        FP1 --> FP2["Iterate AggregateHashTable\nbucket by bucket"]
        FP2 --> FP3["For each group entry:\nfinalize aggregate state"]
        FP3 --> FP4{"Aggregate type\n(finalize step)?"}
        FP4 -- COUNT / SUM / MIN / MAX --> FP5["Final value = state accumulator\n(already final)"]
        FP4 -- AVG --> FP6["Final value = state.sum / state.count"]
        FP5 --> FP7["Emit row: group keys + aggregate results\ninto output DataChunk"]
        FP6 --> FP7
        FP7 --> FP8{"output_chunk\nfull (1024 rows)?"}
        FP8 -- Yes --> FP9["Yield output_chunk\ncontinue iteration"]
        FP8 -- No --> FP2
        FP2 --> FP10{"All groups\nemitted?"}
        FP10 -- No --> FP2
        FP10 -- Yes --> FP11["Aggregation complete"]
    end

    D --> BuildPhase
    E --> BuildPhase
    BuildPhase --> FinalizePhase

    subgraph AggState["AggregateState Structure"]
        AS1["AggregateState per group:\n  count: int64_t\n  sum: double  (for SUM/AVG)\n  min_val: Value\n  max_val: Value"]
    end
```

## Planned Implementation
- `src/execution/operator/hash_aggregation.cpp` — HashAggSink::Consume(), Finalize()
- `src/execution/aggregate_hash_table.cpp` — AggregateHashTable, AggregateState management
- `src/execution/aggregate_functions.cpp` — per-type accumulator update and finalize logic
- `src/planner/physical_planner.cpp` — PhysicalHashAggregation creation
