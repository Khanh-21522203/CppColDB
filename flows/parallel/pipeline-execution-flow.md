# Pipeline Execution Flow

## Assumptions
- CppColDB uses a single-threaded pipeline model for simplicity; no parallel task scheduling within a query.
- Executor initializes one or more pipelines from the physical plan.
- Each pipeline runs to completion before the next starts (unless pipelined).
- Background tasks (checkpoint, compaction) use the thread pool but are separate from query execution.

## Diagram

```mermaid
flowchart TD
    A["Executor::Initialize(physical_plan)"] --> B["Build Pipeline list\nfrom physical operator tree"]
    B --> C["Each pipeline: Source + Operators + Sink"]

    C --> D["Executor::Execute()"]
    D --> E["For each Pipeline in order"]
    E --> F["Pipeline::Initialize()\nset up local state for source/operators/sink"]

    F --> G["Pipeline execution loop"]
    G --> H["Source::GetData(chunk)"]
    H --> I{"Source\nexhausted?"}
    I -- Yes --> J["Pipeline::Finalize()\nflush operators and sink"]
    J --> K{"More\npipelines?"}
    K -- Yes --> E
    K -- No --> L["Execution complete"]

    I -- No --> M["DataChunk from Source\n(up to 1024 rows)"]
    M --> N["Pass chunk through\neach Operator in order"]
    N --> O["Sink::Consume(chunk)"]
    O --> P{"Error\nduring execution?"}
    P -- Yes --> Q["Abort all remaining pipelines\nPropagate error"]
    P -- No --> G

    subgraph PipelineStructure["Pipeline Structure"]
        S1["Source\n(e.g. PhysicalTableScan)"]
        S2["Operator 1\n(e.g. Filter)"]
        S3["Operator 2\n(e.g. Projection)"]
        S4["Sink\n(e.g. ResultCollector or HashJoin build)"]
        S1 --> S2 --> S3 --> S4
    end

    subgraph ResultCollection["Result Collection"]
        R1["ResultCollector sink\naccumulates DataChunks"]
        R2["After Execute():\nExecutor::GetResult()\nreturns all collected chunks"]
    end
```

## Planned Implementation
- `src/execution/executor.cpp` — Executor::Initialize(), Execute(), GetResult()
- `src/execution/pipeline.cpp` — Pipeline, Pipeline::Initialize(), Finalize()
- `src/execution/physical_operator.cpp` — PhysicalOperator base class (Source/Operator/Sink roles)
