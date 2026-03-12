# Feature: SQL Parser

## 1. Purpose

The SQL Parser is the first stage of query processing. It transforms a raw SQL string into a typed AST (`ParsedStatement`) that later stages can traverse without any knowledge of SQL syntax. The parser is hand-written using a recursive descent strategy — no third-party parser library is used. A `Tokenizer` first converts the input string into a flat token stream; the `Parser` then walks that stream using one method per statement type.

## 2. Responsibilities

- `Tokenizer`: scan a SQL string character-by-character and produce a `std::vector<Token>` with typed tokens (keyword, identifier, number literal, string literal, operator, punctuation, end-of-input)
- `Parser`: consume the token stream and dispatch on the leading keyword to build a `ParsedStatement` AST
- Support statement types: SELECT, INSERT, UPDATE, DELETE, CREATE TABLE, DROP TABLE, BEGIN, COMMIT, ROLLBACK, EXPLAIN
- Build correct AST nodes for expressions: column references, literals, binary operators (AND, OR, +, -, *, /, comparison), unary operators (NOT, -), function calls
- Throw `ParseError` with position info on any syntax violation
- Verify that all tokens are consumed after parsing (no trailing junk)

## 3. Non-Responsibilities

- Does not resolve table or column names against the Catalog (that is the Binder's job)
- Does not evaluate expressions at parse time (that is the Optimizer's constant-folding job)
- Does not validate types or perform casts
- Does not handle multiple statements in a single string
- Does not perform any I/O

## 4. Architecture Design

```
ClientContext::Query(sql)
         |
         v
+-------------------+
|    Tokenizer      |  raw SQL string → vector<Token>
|  (lexical scan)   |
+-------------------+
         |
   vector<Token>
         |
         v
+-------------------+
|     Parser        |  token stream → ParsedStatement AST
| (recursive descent|
|  one method per   |
|  statement type)  |
+-------------------+
         |
   ParsedStatement*
   (SelectStatement | InsertStatement | ...)
         |
         v
     Binder (next stage)
```

**Token types:**
```
KEYWORD       — SELECT, FROM, WHERE, INSERT, ...
IDENTIFIER    — unquoted or quoted names
INTEGER_LIT   — 42, -7
FLOAT_LIT     — 3.14
STRING_LIT    — 'hello'
OPERATOR      — =, <, >, <=, >=, <>, +, -, *, /
PUNCTUATION   — (, ), ,, ;, .
END_OF_INPUT
```

## 5. Core Data Structures (C++)

```cpp
// src/parser/token.hpp
#pragma once
#include <string>
#include <cstdint>

namespace cppcoldb {

enum class TokenType : uint8_t {
    KEYWORD,
    IDENTIFIER,
    INTEGER_LIT,
    FLOAT_LIT,
    STRING_LIT,
    OPERATOR,
    PUNCTUATION,
    END_OF_INPUT,
};

struct Token {
    TokenType   type;
    std::string value;   // raw text of the token
    size_t      pos;     // byte offset in original SQL string (for error messages)
};

} // namespace cppcoldb
```

```cpp
// src/parser/ast/parsed_statement.hpp
#pragma once
#include <memory>
#include <vector>
#include <string>
#include <optional>
#include "common/types.hpp"

namespace cppcoldb {

// Base class for all AST expression nodes.
struct Expr {
    enum class Kind {
        COLUMN_REF,    // table.column or just column
        INTEGER_LIT,
        FLOAT_LIT,
        STRING_LIT,
        BOOL_LIT,
        NULL_LIT,
        BINARY_OP,     // left op right
        UNARY_OP,      // op expr
        FUNCTION_CALL, // func(args...)
        STAR,          // SELECT *
    };
    Kind kind;
    virtual ~Expr() = default;
};

struct ColumnRefExpr : Expr {
    std::string table_name;  // empty if unqualified
    std::string column_name;
};

struct IntegerLitExpr : Expr {
    int64_t value;
};

struct FloatLitExpr : Expr {
    double value;
};

struct StringLitExpr : Expr {
    std::string value;
};

struct BoolLitExpr : Expr {
    bool value;
};

struct NullLitExpr  : Expr {};
struct StarExpr     : Expr {};

struct BinaryOpExpr : Expr {
    std::string op;                // "+", "-", "*", "/", "=", "<", ">", "<=", ">=", "<>", "AND", "OR"
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
};

struct UnaryOpExpr : Expr {
    std::string op;                // "NOT", "-"
    std::unique_ptr<Expr> operand;
};

struct FunctionCallExpr : Expr {
    std::string              name;  // "COUNT", "SUM", "MIN", "MAX", "AVG"
    std::vector<std::unique_ptr<Expr>> args;
    bool is_star = false;           // COUNT(*) special case
};

// Column definition used in CREATE TABLE.
struct ColumnDef {
    std::string name;
    TypeId      type;
    bool        not_null    = false;
    bool        primary_key = false;
};

// Base class for all parsed statement nodes.
struct ParsedStatement {
    enum class Type {
        SELECT,
        INSERT,
        UPDATE,
        DELETE,
        CREATE_TABLE,
        DROP_TABLE,
        BEGIN,
        COMMIT,
        ROLLBACK,
        EXPLAIN,
    };
    Type stmt_type;
    virtual ~ParsedStatement() = default;
};

// SELECT [DISTINCT] col_list FROM table [WHERE expr]
//        [GROUP BY exprs] [ORDER BY exprs [ASC|DESC]] [LIMIT n]
struct SelectStatement : ParsedStatement {
    bool                                  distinct   = false;
    std::vector<std::unique_ptr<Expr>>    select_list;  // expressions or StarExpr
    std::string                           from_table;   // simple single-table only
    std::string                           from_schema;  // empty → default "main"
    std::unique_ptr<Expr>                 where_clause; // nullptr if absent
    std::vector<std::unique_ptr<Expr>>    group_by;
    std::vector<std::unique_ptr<Expr>>    order_by;
    std::vector<bool>                     order_asc;    // true = ASC (parallel to order_by)
    std::optional<int64_t>                limit;
    std::optional<int64_t>                offset;
};

struct InsertStatement : ParsedStatement {
    std::string                           table_name;
    std::string                           schema_name;
    std::vector<std::string>              column_names; // empty → all columns
    // Each inner vector is one row of values.
    std::vector<std::vector<std::unique_ptr<Expr>>> values;
};

struct UpdateSetClause {
    std::string              column_name;
    std::unique_ptr<Expr>    value_expr;
};

struct UpdateStatement : ParsedStatement {
    std::string                        table_name;
    std::string                        schema_name;
    std::vector<UpdateSetClause>       set_clauses;
    std::unique_ptr<Expr>              where_clause;
};

struct DeleteStatement : ParsedStatement {
    std::string               table_name;
    std::string               schema_name;
    std::unique_ptr<Expr>     where_clause;
};

struct CreateTableStatement : ParsedStatement {
    std::string            schema_name;
    std::string            table_name;
    std::vector<ColumnDef> columns;
    bool                   if_not_exists = false;
};

struct DropTableStatement : ParsedStatement {
    std::string schema_name;
    std::string table_name;
    bool        if_exists = false;
};

struct TransactionStatement : ParsedStatement {
    // stmt_type encodes BEGIN / COMMIT / ROLLBACK
};

struct ExplainStatement : ParsedStatement {
    bool                               analyze = false; // EXPLAIN ANALYZE
    std::unique_ptr<ParsedStatement>   inner;
};

} // namespace cppcoldb
```

## 6. Public Interfaces

```cpp
// src/parser/tokenizer.hpp
namespace cppcoldb {
class Tokenizer {
public:
    explicit Tokenizer(std::string_view sql);
    // Tokenize the full input; throws ParseError on unrecognized characters.
    std::vector<Token> Tokenize();
};
} // namespace cppcoldb

// src/parser/parser.hpp
namespace cppcoldb {
class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    // Parse the token stream into a single ParsedStatement.
    // Throws ParseError if the SQL is syntactically invalid.
    std::unique_ptr<ParsedStatement> Parse();
private:
    std::unique_ptr<SelectStatement>      ParseSelect();
    std::unique_ptr<InsertStatement>      ParseInsert();
    std::unique_ptr<UpdateStatement>      ParseUpdate();
    std::unique_ptr<DeleteStatement>      ParseDelete();
    std::unique_ptr<CreateTableStatement> ParseCreateTable();
    std::unique_ptr<DropTableStatement>   ParseDropTable();
    std::unique_ptr<TransactionStatement> ParseTransaction();
    std::unique_ptr<ExplainStatement>     ParseExplain();

    // Expression parsing (recursive descent, handles precedence)
    std::unique_ptr<Expr> ParseExpr();
    std::unique_ptr<Expr> ParseOr();
    std::unique_ptr<Expr> ParseAnd();
    std::unique_ptr<Expr> ParseNot();
    std::unique_ptr<Expr> ParseComparison();
    std::unique_ptr<Expr> ParseAddSub();
    std::unique_ptr<Expr> ParseMulDiv();
    std::unique_ptr<Expr> ParseUnary();
    std::unique_ptr<Expr> ParsePrimary();

    // Token stream helpers
    Token&      Peek();
    Token       Consume();
    Token       Expect(TokenType t, std::string_view value = "");
    bool        Check(TokenType t, std::string_view value = "") const;
    bool        Match(TokenType t, std::string_view value = "");
};
} // namespace cppcoldb
```

## 7. Internal Algorithms

### Tokenizer scan loop
```
Tokenize(sql):
  pos = 0
  tokens = []
  while pos < len(sql):
    1. Skip whitespace
    2. If letter or '_': scan identifier/keyword word
       - uppercase and compare against keyword set
       - if match: emit KEYWORD token
       - else: emit IDENTIFIER token
    3. If digit: scan integer or float literal
       - if '.' encountered: emit FLOAT_LIT
       - else: emit INTEGER_LIT
    4. If "'": scan string literal until closing "'"
       - emit STRING_LIT (value excludes quotes)
    5. If two-char operator ('<=', '>=', '<>'): emit OPERATOR
    6. If single-char operator or punctuation: emit OPERATOR or PUNCTUATION
    7. Else: throw ParseError("unrecognized character at pos")
  emit END_OF_INPUT
  return tokens
  Time: O(n) where n = len(sql)
```

### Recursive descent expression precedence
```
Lowest precedence at top, highest at bottom:
  ParseExpr → ParseOr → ParseAnd → ParseNot
  → ParseComparison → ParseAddSub → ParseMulDiv
  → ParseUnary → ParsePrimary
Each level: call next level, then loop on matching operators at that level.
Example ParseAddSub:
  left = ParseMulDiv()
  while Peek() is '+' or '-':
    op = Consume().value
    right = ParseMulDiv()
    left = BinaryOpExpr(op, left, right)
  return left
  Time: O(tokens)
```

### Parser::Parse() dispatch
```
Parse():
  1. peek = Peek()
  2. match keyword:
     SELECT        -> ParseSelect()
     INSERT INTO   -> ParseInsert()
     UPDATE        -> ParseUpdate()
     DELETE FROM   -> ParseDelete()
     CREATE TABLE  -> ParseCreateTable()
     DROP TABLE    -> ParseDropTable()
     BEGIN         -> ParseTransaction(BEGIN)
     COMMIT        -> ParseTransaction(COMMIT)
     ROLLBACK      -> ParseTransaction(ROLLBACK)
     EXPLAIN       -> ParseExplain()
     else          -> throw ParseError("unexpected token")
  3. if Peek() is not END_OF_INPUT:
       throw ParseError("unexpected trailing tokens")
  4. return statement
```

## 8. Persistence Model

Not applicable. The parser is stateless; no data is persisted.

## 9. Concurrency Model

`Tokenizer` and `Parser` are stateful but not shared across threads. Each call to `ClientContext::Query()` constructs fresh instances. No locks needed.

## 10. Configuration

No runtime configuration. The vector size and keyword table are compile-time constants.

```cpp
// Keyword lookup table (static constexpr array of string_view)
static constexpr std::string_view KEYWORDS[] = {
    "SELECT", "FROM", "WHERE", "INSERT", "INTO", "UPDATE", "SET",
    "DELETE", "CREATE", "TABLE", "DROP", "BEGIN", "COMMIT", "ROLLBACK",
    "EXPLAIN", "ANALYZE", "AND", "OR", "NOT", "NULL", "TRUE", "FALSE",
    "GROUP", "BY", "ORDER", "ASC", "DESC", "LIMIT", "OFFSET",
    "VALUES", "PRIMARY", "KEY", "DISTINCT", "IF", "EXISTS", "SCHEMA",
    "JOIN", "ON", "INNER", "LEFT", "RIGHT", "OUTER", "CROSS",
    "AS", "IN", "IS", "LIKE", "BETWEEN", "HAVING",
};
```

## 11. Testing Strategy

- `TestTokenizerSimpleSelect`: tokenize `SELECT a FROM t` → verify keyword/identifier token sequence
- `TestTokenizerStringLiteral`: verify string literal with spaces is captured verbatim
- `TestTokenizerFloatLiteral`: `3.14` → FLOAT_LIT with value "3.14"
- `TestTokenizerTwoCharOp`: `<>`, `<=`, `>=` produce single OPERATOR tokens
- `TestTokenizerUnknownChar`: `@` throws ParseError with position info
- `TestParseSelect`: `SELECT id, name FROM orders WHERE id > 5 LIMIT 10` → SelectStatement with correct fields
- `TestParseSelectStar`: `SELECT * FROM t` → select_list has StarExpr
- `TestParseInsertValues`: `INSERT INTO t (a,b) VALUES (1,'x'),(2,'y')` → InsertStatement with 2 rows
- `TestParseUpdate`: `UPDATE t SET a=1 WHERE b=2` → UpdateStatement with one SetClause
- `TestParseDelete`: `DELETE FROM t WHERE id=3` → DeleteStatement with where clause
- `TestParseCreateTable`: `CREATE TABLE s.t (id INT64 PRIMARY KEY, name VARCHAR NOT NULL)` → correct ColumnDefs
- `TestParseDropTableIfExists`: `DROP TABLE IF EXISTS t` → if_exists == true
- `TestParseBeginCommitRollback`: each produces TransactionStatement with correct stmt_type
- `TestParseExplainAnalyze`: `EXPLAIN ANALYZE SELECT 1` → ExplainStatement{analyze=true}
- `TestParseExprPrecedence`: `1 + 2 * 3` parses with MulDiv binding tighter than AddSub
- `TestParseTrailingTokensError`: `SELECT 1 GARBAGE` throws ParseError
- `TestParseUnknownStatementError`: `FOOBAR` throws ParseError

## 12. Open Questions

- JOIN syntax: the plan scopes to single-table FROM for initial implementation; join parsing may be added later when the binder supports it.
- Subqueries in FROM or WHERE: deferred to a future iteration.
