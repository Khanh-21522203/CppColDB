# SQL Parsing Flow

## Assumptions
- CppColDB uses a hand-written recursive descent parser; no third-party parser library.
- The Tokenizer converts the raw SQL string into a token stream.
- The Parser walks the token stream and builds a typed AST (ParsedStatement).
- Single-statement focus: one SQL string produces one ParsedStatement.

## Diagram

```mermaid
flowchart TD
    A["ClientContext::Parse(sql)"] --> B["Tokenizer::Tokenize(sql)"]
    B --> C{"Tokenize\nsucceeded?"}
    C -- No --> D["Throw ParseError:\nunrecognized token at position"]
    C -- Yes --> E["token_stream\n(list of typed tokens)"]

    E --> F["Parser::Parse(token_stream)"]
    F --> G["Peek first token"]
    G --> H{"Statement type?"}

    H -- SELECT --> I["ParseSelect()\nrecursive descent"]
    H -- INSERT --> J["ParseInsert()"]
    H -- UPDATE --> K["ParseUpdate()"]
    H -- DELETE --> L["ParseDelete()"]
    H -- CREATE TABLE --> M["ParseCreateTable()"]
    H -- DROP TABLE --> N["ParseDropTable()"]
    H -- BEGIN --> O["ParseTransaction()"]
    H -- COMMIT --> O
    H -- ROLLBACK --> O
    H -- EXPLAIN --> P["ParseExplain()"]
    H -- Unknown --> Q["Throw ParseError:\nunexpected token"]

    I --> R["Build ParsedStatement AST"]
    J --> R
    K --> R
    L --> R
    M --> R
    N --> R
    O --> R
    P --> R

    R --> S{"Tokens fully\nconsumed?"}
    S -- No --> T["Throw ParseError:\nunexpected trailing tokens"]
    S -- Yes --> U["Return ParsedStatement"]

    subgraph ASTNodes["Key AST Node Types"]
        N1["SelectStatement\n(columns, from, where, group by, order by, limit)"]
        N2["InsertStatement\n(table, columns, values)"]
        N3["UpdateStatement\n(table, set_clauses, where)"]
        N4["DeleteStatement\n(table, where)"]
        N5["CreateTableStatement\n(schema, name, columns, constraints)"]
        N6["TransactionStatement\n(BEGIN / COMMIT / ROLLBACK)"]
    end
```

## Planned Implementation
- `src/parser/tokenizer.cpp` — Tokenizer, Token types
- `src/parser/parser.cpp` — Parser, recursive descent methods
- `src/parser/ast/` — AST node types (ParsedStatement and subtypes)
