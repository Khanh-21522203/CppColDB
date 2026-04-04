# Execution Pipelines and Physical Operators

## Purpose

Execute physical plans using vectorized `DataChunk` batches, pipeline dependencies, and operator role contracts (`SOURCE`, `OPERATOR`, `SINK`).

## Scope

**In scope:**
- Pipeline graph construction (`Executor::BuildPipelines`).
- Runtime chunk flow (`PipelineExecutor::Execute`).
- Core operator contract and return codes.
- Result collection into `QueryResult`.

**Out of scope:**
- SQL parsing and planning stages that create physical plans.
- Storage layout internals.

## Primary User Flow

1. `Executor::Initialize(plan)` stores physical root and creates top pipeline with `PhysicalResultCollector` sink.
2. `Executor::BuildPipelines` discovers dependencies (hash join build pipeline, sort/agg consume pipelines).
3. `Executor::Execute` topologically orders pipelines and runs each with `PipelineExecutor`.
4. Caller retrieves results with `Executor::GetResult()`.

## System Flow

1. `Executor::Initialize` creates collector with root output schema.
2. `BuildPipelines` walks operator tree:
- linear path: source -> operators -> sink
- hash join: separate build pipeline for `PhysicalHashJoinBuild`
- hash agg and sort: consume pipeline plus source pipeline (`PhysicalAggregationSource` / `PhysicalSortSource`)
3. `PipelineExecutor::Execute` loops source `GetData`, pushes chunks through operators, and calls sink `Consume`/`Finalize`.
4. Operator return codes (`OperatorResultType`) drive control:
- `NEED_MORE_INPUT`: fetch next source chunk
- `HAVE_MORE_OUTPUT`: call again for same input
- `FINISHED`: terminate pipeline early
5. `PhysicalResultCollector::Consume` deep-copies output chunks into `QueryResult.chunks`.

```
Physical plan root
  -> Executor::BuildPipelines
     -> [Pipeline DAG]
  -> topological order
     -> PipelineExecutor per pipeline
        -> SOURCE.GetData
        -> OPERATOR.Execute / TryFlush
        -> SINK.Consume
  -> QueryResult via PhysicalResultCollector
```

## Data Model

- `Pipeline` (`src/execution/pipeline.hpp`):
- `source (PhysicalOperator*)`
- `operators (std::vector<PhysicalOperator*>)`
- `sink (PhysicalOperator*)`
- `dependencies (std::vector<Pipeline*>)`
- `PhysicalOperator` (`src/execution/physical_operator.hpp`):
- `role`, `output_types`, `output_names`, `children`, `profile_idx`
- role methods: `InitScan/GetData`, `Execute/TryFlush`, `Consume/Finalize`
- `OperatorResultType` (`src/execution/operator_result_type.hpp`): `FINISHED`, `HAVE_MORE_OUTPUT`, `NEED_MORE_INPUT`
- `QueryResult` (`src/execution/physical_result_collector.hpp`):
- `column_names`, `column_types`, `chunks`, `success`, `error_message`, optional `profiling_result`

## Interfaces and Contracts

- `Executor::Initialize(std::unique_ptr<PhysicalOperator>)`
- contract: must be called before `Execute`.
- `Executor::Execute()`
- contract: runs all pipelines in dependency order.
- `PipelineExecutor::Execute()`
- contract: drives one pipeline to completion and calls sink `Finalize`.
- Representative operator contracts:
- `PhysicalTableScan::GetData` produces scan chunks.
- `PhysicalFilter::Execute` compacts selected rows.
- `PhysicalProjection::Execute` evaluates output expressions.
- `PhysicalLimit::Execute` can return `FINISHED` to short-circuit pipeline.
- `PhysicalInsert/Update/Delete/CreateTable/DropTable/AlterTable` execute as SOURCE or SINK mutation operators.

## Dependencies

**Internal modules:**
- `src/execution/operator/*` implementations.
- `src/execution/join_hash_table.*` and `aggregate_hash_table.*` shared runtime structures.
- `src/main/client_context.hpp` for catalog/transaction/profiler context.
- `src/profiler/operator_profiler.hpp` for operator-level timing.

**External services/libraries:**
- None.

## Failure Modes and Edge Cases

- Mutation operators throw `RuntimeError` if `ClientContext` lacks `catalog` or `transaction`.
- `PhysicalTableScan::GetData` returns `FINISHED` when table/context is missing (soft fail read path).
- Hash join verifies full key equality after hash match to avoid collision false positives.
- Scalar aggregate over empty input emits one synthetic row via `PhysicalAggregationSource` (`COUNT(*)=0`, others NULL).
- `PipelineExecutor` has explicit flush path (`TryFlushOperators`) for buffered operators after source exhaustion.

## Observability and Debugging

- Operators are registered with `QueryProfiler` in `Executor::Execute` via demangled type names.
- `OperatorProfileGuard` captures per-call timing, `rows_in`, `rows_out`.
- Debug entry points:
- pipeline construction: `src/execution/executor.cpp:BuildPipelines`
- chunk flow/control states: `src/execution/pipeline_executor.cpp`

## Risks and Notes

- Correctness relies on each operator honoring `OperatorResultType` semantics; mismatched behavior can break pipeline flow.
- Some operators (join/aggregation/sort) maintain shared state across paired pipelines; wiring in planner/executor must stay synchronized.

Changes:

