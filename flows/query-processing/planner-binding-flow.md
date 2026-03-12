# Planner & Binding Flow

## Assumptions
- The Binder takes the parsed AST and resolves all table/column references against the Catalog.
- Binding fails with a BindError if a referenced table or column does not exist.
- The output of binding is a fully typed LogicalPlan.
- The LogicalPlanner coordinates the overall planner → binder → plan construction sequence.

## Diagram

```mermaid
flowchart TD
    A["LogicalPlanner::Plan(ParsedStatement)"] --> B["Create Binder\nwith ClientContext + Catalog"]
    B --> C["Binder::Bind(statement)"]

    C --> D{"Statement type?"}
    D -- SELECT --> E["BindSelect()"]
    D -- INSERT --> F["BindInsert()"]
    D -- UPDATE --> G["BindUpdate()"]
    D -- DELETE --> H["BindDelete()"]
    D -- CREATE TABLE --> I["BindCreateTable()"]
    D -- DROP TABLE --> J["BindDropTable()"]
    D -- EXPLAIN --> K["BindExplain()"]
    D -- Transaction --> L["BindTransaction()"]

    E --> M["Resolve FROM clause:\nCatalog::GetEntry(schema, table_name, tx)"]
    F --> M
    G --> M
    H --> M

    M --> N{"Table found\nin Catalog?"}
    N -- No --> O["Throw BindError:\ntable not found"]
    N -- Yes --> P["CatalogEntry returned\nwith column list + types"]

    P --> Q["Resolve column references\nvia BindContext"]
    Q --> R{"All columns\nresolved?"}
    R -- No --> S["Throw BindError:\ncolumn not found"]
    R -- Yes --> T["Type-check expressions\n(implicit casts as needed)"]

    T --> U{"Type errors?"}
    U -- Yes --> V["Throw BindError:\ntype mismatch"]
    U -- No --> W["Build LogicalPlan nodes"]

    I --> X["Validate column definitions\nand constraints"]
    J --> Y["Check table exists\nand drop permissions"]
    K --> Z["Bind inner statement"]
    L --> AA["No catalog resolution needed"]

    W --> AB["Return LogicalPlan\n(fully typed, all refs resolved)"]
    X --> AB
    Y --> AB
    Z --> AB
    AA --> AB

    subgraph LogicalNodes["Key LogicalPlan Node Types"]
        LN1["LogicalGet\n(table scan with column projection)"]
        LN2["LogicalFilter\n(predicate expression)"]
        LN3["LogicalProjection\n(expression list)"]
        LN4["LogicalJoin\n(join type + condition)"]
        LN5["LogicalAggregate\n(group keys + aggregate exprs)"]
        LN6["LogicalInsert / LogicalDelete / LogicalUpdate"]
    end
```

## Planned Implementation
- `src/planner/logical_planner.cpp` — LogicalPlanner::Plan()
- `src/planner/binder.cpp` — Binder::Bind(), BindContext
- `src/catalog/catalog.cpp` — Catalog::GetEntry()
- `src/planner/logical_plan/` — LogicalPlan node types
