# Pipeline Executor (Per-Pipeline) Flow

## Assumptions
- A PipelineExecutor processes one pipeline: Source produces DataChunks, each Operator transforms them, Sink consumes the result.
- Vectorized chunk-at-a-time model: all operators process a full DataChunk (up to 1024 rows) per call.
- Each operator maintains local state (e.g. hash table, sort buffer) initialized before the loop.

## Diagram

```mermaid
flowchart TD
    A["PipelineExecutor::Execute(pipeline)"] --> B["Initialize local states\nfor each operator and sink"]
    B --> C["Source::InitScan(scan_state)"]

    C --> D["--- Chunk Processing Loop ---"]
    D --> E["chunk.Reset()\nclear previous data"]
    E --> F["Source::GetData(scan_state, chunk)"]
    F --> G{"Source\nresult?"}

    G -- FINISHED --> H["TryFlushOperators()\nflush any buffered operator state"]
    G -- HAVE_MORE_OUTPUT --> I["Execute operators\non this chunk"]

    I --> J["For each Operator in pipeline"]
    J --> K["Operator::Execute(input_chunk, output_chunk)"]
    K --> L{"Operator\nresult?"}
    L -- HAS_MORE_OUTPUT --> M["Pass output_chunk\nto next operator or sink"]
    L -- NEEDS_MORE_INPUT --> N["Get next chunk from source\n(this chunk fully consumed)"]
    L -- FINISHED --> O["Operator signaled early stop\n(e.g. LIMIT reached)"]

    M --> P{"Last operator\nin chain?"}
    P -- No --> J
    P -- Yes --> Q["Sink::Consume(final_chunk)"]
    Q --> R{"More source\ndata?"}
    R -- Yes --> D
    R -- No --> H

    N --> D
    O --> H

    H --> S["Sink::Finalize()\nflush buffered sink state\n(e.g. sort, hash agg finalize)"]
    S --> T["Pipeline execution complete"]

    subgraph DataChunkStructure["DataChunk Structure"]
        DC1["DataChunk:\n  vector&lt;DataVector&gt; columns\n  size_t count  (rows in this batch)"]
        DC2["DataVector:\n  type (INT32, VARCHAR, FLOAT64, ...)\n  data pointer  (contiguous column array)\n  validity bitmask  (NULL flags)"]
    end
```

## Planned Implementation
- `src/execution/pipeline_executor.cpp` — PipelineExecutor::Execute()
- `src/execution/physical_operator.cpp` — GetData(), Execute(), Consume(), Finalize()
- `src/common/data_chunk.cpp` — DataChunk, DataVector
