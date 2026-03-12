# Query Execution Flow

## Assumptions
- Full path from Connection::Query() through parse → bind → optimize → physical plan → execute.
- Auto-commit: a transaction is started before execution and committed (or rolled back) after.
- Single-statement focus: one SQL string produces one result.

## Diagram

```mermaid
flowchart TD
    A["Connection::Query(sql)"] --> B["ClientContext::Query(sql)"]
    B --> C["Parser::Parse(sql)"]
    C --> D{"Parse\nsucceeded?"}
    D -- No --> E["Return error result\n(ParseError)"]
    D -- Yes --> F["ParsedStatement"]

    F --> G["TransactionManager::\nBeginTransaction()"]
    G --> H["Transaction assigned\ntx_id and start_time"]

    H --> I["LogicalPlanner::Plan(statement)"]
    I --> J["Binder::Bind(statement)"]
    J --> K{"Binding\nsucceeded?"}
    K -- No --> L["Rollback transaction\nReturn error (BindError)"]
    K -- Yes --> M["LogicalPlan"]

    M --> N["Optimizer::Optimize(logical_plan)"]
    N --> O["Optimized LogicalPlan"]

    O --> P["PhysicalPlanner::Plan(logical_plan)"]
    P --> Q["PhysicalPlan\n(tree of physical operators)"]

    Q --> R["Executor::Initialize(physical_plan)"]
    R --> S["Build Pipeline(s)\nfrom operator tree"]
    S --> T["Executor::Execute()"]

    T --> U["Pipeline execution loop\n(see pipeline-execution-flow.md)"]
    U --> V{"Execution\nsucceeded?"}

    V -- No --> W["Rollback transaction\nReturn error (RuntimeError/IOError)"]
    V -- Yes --> X["Collect result from\nResultCollector sink"]

    X --> Y{"Auto-commit?"}
    Y -- Yes --> Z["TransactionManager::Commit()"]
    Y -- No --> AA["Keep transaction open"]
    Z --> AB["Return QueryResult\n(column names, types, data chunks)"]
    AA --> AB

    subgraph ErrorTypes["Error Classifications"]
        E1["ParseError — bad SQL syntax"]
        E2["BindError — unknown table/column"]
        E3["RuntimeError — execution failure"]
        E4["IOError — disk/storage failure"]
    end
```

## Planned Implementation
- `src/main/connection.cpp` — Connection::Query()
- `src/main/client_context.cpp` — ClientContext::Query()
- `src/parser/parser.cpp` — Parser::Parse()
- `src/planner/logical_planner.cpp` — LogicalPlanner::Plan()
- `src/optimizer/optimizer.cpp` — Optimizer::Optimize()
- `src/planner/physical_planner.cpp` — PhysicalPlanner::Plan()
- `src/execution/executor.cpp` — Executor::Initialize(), Execute()
