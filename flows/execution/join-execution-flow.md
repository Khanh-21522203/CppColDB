# Join Execution Flow

## Assumptions
- The primary join algorithm is hash join (build + probe phases).
- Hash join is implemented as two pipelines: one to build the hash table, one to probe it.
- Nested-loop join is available as a fallback for non-equi joins or small relations.
- Join output is vectorized: matching rows are emitted as DataChunks.

## Diagram

```mermaid
flowchart TD
    A["LogicalJoin node in plan"] --> B["PhysicalPlanner: choose join algorithm"]
    B --> C{"Join type\nand conditions?"}
    C -- "Equi-join AND\nbuild side fits in memory" --> D["Hash Join"]
    C -- "Non-equi join OR\nvery small right side" --> E["Nested-Loop Join"]

    subgraph HashJoinBuild["Hash Join — Build Phase (Pipeline 1)"]
        HB1["Source: Scan right (build) relation"]
        HB1 --> HB2["HashJoinBuildSink::Consume(chunk)"]
        HB2 --> HB3["For each row in chunk:\ncompute hash of join key columns"]
        HB3 --> HB4["Insert row into HashTable\nhash_table[key_hash] = row_data"]
        HB4 --> HB5{"More build\nchunks?"}
        HB5 -- Yes --> HB1
        HB5 -- No --> HB6["HashTable fully built\nMark build pipeline complete"]
    end

    subgraph HashJoinProbe["Hash Join — Probe Phase (Pipeline 2)"]
        HP1["Source: Scan left (probe) relation"]
        HP1 --> HP2["HashJoinProbeOperator::Execute(input_chunk, output_chunk)"]
        HP2 --> HP3["For each row in input_chunk:\ncompute hash of probe key columns"]
        HP3 --> HP4["Probe HashTable:\nlook up hash_table[key_hash]"]
        HP4 --> HP5{"Hash bucket\nfound?"}
        HP5 -- No --> HP6["No match: skip (INNER JOIN)\nor emit NULLs (LEFT JOIN)"]
        HP5 -- Yes --> HP7["Compare full key\n(handle hash collisions)"]
        HP7 --> HP8{"Keys match?"}
        HP8 -- No --> HP6
        HP8 -- Yes --> HP9["Emit combined row:\nprobe columns + build columns\ninto output_chunk"]
        HP9 --> HP10{"output_chunk\nfull (1024 rows)?"}
        HP10 -- Yes --> HP11["Return HAVE_MORE_OUTPUT\nwith current output_chunk"]
        HP10 -- No --> HP3
        HP6 --> HP3
    end

    subgraph NestedLoopJoin["Nested-Loop Join (Fallback)"]
        NL1["For each chunk from left (outer) relation"]
        NL1 --> NL2["For each row in left chunk"]
        NL2 --> NL3["Scan entire right (inner) relation"]
        NL3 --> NL4["For each right row:\nevaluate join condition"]
        NL4 --> NL5{"Condition\ntrue?"}
        NL5 -- Yes --> NL6["Emit combined row\nto output_chunk"]
        NL5 -- No --> NL7["Continue to next right row"]
        NL6 --> NL7
        NL7 --> NL3
        NL3 --> NL2
        NL2 --> NL1
    end

    D --> HashJoinBuild
    HashJoinBuild --> HashJoinProbe
    E --> NestedLoopJoin
```

## Planned Implementation
- `src/execution/operator/hash_join_build.cpp` — HashJoinBuildSink, HashTable construction
- `src/execution/operator/hash_join_probe.cpp` — HashJoinProbeOperator, probe logic
- `src/execution/operator/nested_loop_join.cpp` — NestedLoopJoin fallback
- `src/execution/hash_table.cpp` — HashTable data structure
- `src/planner/physical_planner.cpp` — join algorithm selection
