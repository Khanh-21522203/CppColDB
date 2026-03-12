# Vectorized Execution Flow

## Assumptions
- CppColDB uses vectorized (columnar batch) execution: operators process DataChunks of up to 1024 rows.
- Each DataChunk holds a vector of DataVectors (one per column), plus a row count.
- Processing a whole batch of values per column call enables SIMD-friendly code and better cache usage.
- Operators never process one row at a time in the hot path.

## Diagram

```mermaid
flowchart TD
    subgraph DataChunkStructure["DataChunk Structure"]
        DC["DataChunk\n  count: size_t  (rows in batch, e.g. 1024)\n  columns: vector&lt;DataVector&gt;"]
        DC --> DV0["DataVector[0]\n  type: INT64\n  data: int64_t[1024]\n  validity: bitset&lt;1024&gt;  (NULL flags)"]
        DC --> DV1["DataVector[1]\n  type: VARCHAR\n  data: string_view[1024]\n  validity: bitset&lt;1024&gt;"]
        DC --> DVN["DataVector[N]\n  type: FLOAT64\n  data: double[1024]\n  validity: bitset&lt;1024&gt;"]
    end

    subgraph SourceFill["Source: Fill DataChunk"]
        SF1["PhysicalTableScan::GetData(chunk)"]
        SF1 --> SF2["For each requested column"]
        SF2 --> SF3["ColumnChunk::Scan()\ndecompress block into DataVector"]
        SF3 --> SF4["Set chunk.count = rows read\n(up to 1024)"]
        SF4 --> SF5["Return HAVE_MORE_OUTPUT\nor FINISHED"]
    end

    subgraph FilterOperator["Filter Operator: Apply Predicate over Full Column"]
        FO1["FilterOperator::Execute(input_chunk, output_chunk)"]
        FO1 --> FO2["Evaluate predicate over\ninput column vectors\n(e.g. col[i] > 100 for i in 0..count)"]
        FO2 --> FO3["Build selection vector:\nbool[1024] or list of matching row indices"]
        FO3 --> FO4["Compact matching rows\ninto output_chunk columns"]
        FO4 --> FO5["output_chunk.count = matching_rows"]
    end

    subgraph ProjectionOperator["Projection Operator: Evaluate Expressions over Vectors"]
        PO1["ProjectionOperator::Execute(input_chunk, output_chunk)"]
        PO1 --> PO2["For each output expression"]
        PO2 --> PO3{"Expression type?"}
        PO3 -- ColumnRef --> PO4["Copy DataVector reference\nfrom input to output"]
        PO3 -- BinaryExpr --> PO5["Vectorized operation:\noutput[i] = left[i] + right[i]\nfor i in 0..count"]
        PO3 -- Constant --> PO6["Fill output DataVector\nwith constant value"]
        PO4 --> PO7["output_chunk assembled"]
        PO5 --> PO7
        PO6 --> PO7
    end

    subgraph SinkAccumulate["Sink: Accumulate Result Chunks"]
        SK1["ResultCollector::Consume(chunk)"]
        SK1 --> SK2["Copy chunk into\nresult_chunks list"]
        SK2 --> SK3["After all chunks:\nResultCollector::Finalize()\nReturn list of DataChunks as QueryResult"]
    end

    SourceFill --> FilterOperator
    FilterOperator --> ProjectionOperator
    ProjectionOperator --> SinkAccumulate
```

## Key Performance Properties
- Processing 1024 rows per call amortizes function call overhead
- Column vectors fit in CPU cache (1024 * 8 bytes = 8 KB per INT64 column)
- Predicate evaluation over a full column vector enables compiler auto-vectorization (SIMD)
- NULL handling via validity bitmask avoids branches in the hot loop

## Planned Implementation
- `src/common/data_chunk.cpp` — DataChunk, DataVector
- `src/execution/operator/filter_operator.cpp` — vectorized predicate evaluation
- `src/execution/operator/projection_operator.cpp` — vectorized expression evaluation
- `src/execution/operator/result_collector.cpp` — result accumulation sink
