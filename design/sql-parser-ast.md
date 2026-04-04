# SQL Parser and AST

## Purpose

Convert one SQL statement string into typed AST nodes (`ParsedStatement` and `Expr`) used by the binder.

## Scope

**In scope:**
- Tokenization (`Tokenizer::Tokenize`).
- Recursive-descent parse (`Parser::Parse`, `Parse*` methods).
- AST node definitions in `src/parser/ast/parsed_statement.hpp`.

**Out of scope:**
- Semantic validation against catalog/schema (binder responsibility).
- Logical/physical planning and execution.

## Primary User Flow

1. Caller constructs `Tokenizer(sql)`.
2. Caller runs `Tokenize()` to produce `std::vector<Token>` ending in `END_OF_INPUT`.
3. Caller constructs `Parser(tokens)` and calls `Parse()`.
4. Parser returns one `std::unique_ptr<ParsedStatement>` or throws `ParseError`.

## System Flow

1. `Tokenizer::Tokenize` classifies bytes into keywords, identifiers, literals, operators, punctuation (`src/parser/tokenizer.cpp`).
2. `Parser::Parse` calls `ParseStatement`, allows one optional trailing `;`, and rejects trailing tokens.
3. Statement-specific parsers build AST objects:
- `ParseSelect`, `ParseInsert`, `ParseUpdate`, `ParseDelete`
- `ParseCreateTable`, `ParseDropTable`, `ParseAlterTable`
- `ParseTransaction`, `ParseExplain`
4. Expression parser (`ParseOr` -> `ParseAnd` -> `ParseNot` -> ... -> `ParsePrimary`) creates expression trees.

```
SQL text
  -> Tokenizer::Tokenize
     -> [Token{type,value,pos}... END_OF_INPUT]
  -> Parser::Parse
     -> ParseStatement + recursive expression parse
  -> ParsedStatement AST
```

## Data Model

- `Token` (`src/parser/token.hpp`):
- `type (TokenType)`
- `value (std::string)`
- `pos (size_t byte offset)`
- Core expression nodes (`parsed_statement.hpp`):
- `ColumnRefExpr`, `IntegerLitExpr`, `FloatLitExpr`, `StringLitExpr`, `BoolLitExpr`, `NullLitExpr`, `BinaryOpExpr`, `UnaryOpExpr`, `FunctionCallExpr`, `StarExpr`
- Statement nodes:
- `SelectStatement`, `InsertStatement`, `UpdateStatement`, `DeleteStatement`
- `CreateTableStatement`, `DropTableStatement`, `AlterTableStatement`
- `TransactionStatement`, `ExplainStatement`
- Partition AST fields in `CreateTableStatement`:
- `partition_kind`, `partition_cols`, `hash_partition_count`
- `range_bounds`, `list_values`

## Interfaces and Contracts

- `std::vector<Token> Tokenizer::Tokenize()` (`src/parser/tokenizer.cpp`)
- contract: throws `ParseError` on unrecognized characters or unterminated string literals.
- `std::unique_ptr<ParsedStatement> Parser::Parse()` (`src/parser/parser.cpp`)
- contract: parses exactly one statement (+ optional semicolon), otherwise throws `ParseError`.
- `TypeId Parser::ParseTypeName()`
- contract: supports aliases (`INT`, `INTEGER`, `FLOAT`, `DOUBLE`, `BOOL`, `TEXT`).

## Dependencies

**Internal modules:**
- `src/common/exception.hpp` - `ParseError`.
- `src/common/types.hpp` - `TypeId` used by column definitions.

**External services/libraries:**
- None.

## Failure Modes and Edge Cases

- Multi-statement SQL like `SELECT 1; SELECT 2` fails with trailing-token `ParseError`.
- JOIN parsing supports `INNER JOIN` and bare `JOIN`; tokenizer recognizes `LEFT/RIGHT/CROSS` keywords but parser does not handle them.
- `DISTINCT` is parsed into `SelectStatement.distinct`, but downstream planner support is limited.
- String literal escape comment says `''` handling, but implementation loop condition (`sql_[pos] != '\''`) means doubled-quote escape path is unreachable.
- Transaction statements are parsed, but binder rejects `BEGIN/COMMIT/ROLLBACK`; runtime support comes from `Connection::Query` keyword interception.

## Observability and Debugging

- `ParseError` includes token position (`ParseError::Position`).
- Debug starting points:
- lexical issues: `src/parser/tokenizer.cpp`
- syntax issues: `src/parser/parser.cpp` (especially `Expect`, `ParseStatement`, and `ParsePrimary`)

## Risks and Notes

- Grammar accepts only one SQL statement at a time; no script execution support.
- Parser supports partition DDL syntax for RANGE/HASH/LIST including composite keys and `ALTER TABLE ... ADD/DROP PARTITION`.

Changes:

