# Feature: Binder

## 1. Purpose

The Binder performs semantic analysis on a `ParsedStatement` AST and produces a fully typed `LogicalPlan`. It resolves every table and column reference against the `Catalog`, verifies types in all expressions, inserts implicit casts where needed, and throws a `BindError` with a descriptive message on any semantic violation. The output `LogicalPlan` no longer contains string names — all references are resolved to catalog pointers and column indices.

## 2. Responsibilities

- Accept a `ParsedStatement*` and return a `LogicalPlan` (unique_ptr to a LogicalPlan node tree)
- Resolve FROM-clause table names via `Catalog::GetEntry(schema, table, tx)` with MVCC awareness
- Populate a `BindContext` mapping column names (and optionally table-qualified names) to `(column_index, TypeId)` pairs
- Resolve each column reference in SELECT, WHERE, GROUP BY, ORDER BY, SET expressions against the BindContext
- Type-check all binary/unary expressions; throw `BindError` on incompatible types
- Insert implicit numeric promotions (e.g., INT32 operand in a FLOAT64 expression) as CastExpr nodes
- Bind aggregate functions (COUNT, SUM, MIN, MAX, AVG) and verify argument types
- Validate CREATE TABLE column definitions (no duplicate names, at least one column, valid types)
- For DROP TABLE: confirm the table exists (or that IF EXISTS suppresses the error)
- Produce a `LogicalPlan` node tree with correct type annotations on every expression node

## 3. Non-Responsibilities

- Does not rewrite or optimize the plan (that is the Optimizer's job)
- Does not access storage to read actual data rows
- Does not handle physical execution concerns
- Does not manage transactions (the ClientContext owns the transaction)
- Does not parse SQL syntax

## 4. Architecture Design

```
LogicalPlanner::Plan(ParsedStatement)
         |
         v
+-------------------+        +-------------------+
|     Binder        |------->|     Catalog       |
|  (semantic        |  MVCC  |  GetEntry(schema, |
|   analysis)       |  aware |  table, tx)       |
|                   |        +-------------------+
|  BindContext      |
|  (name -> col_idx)|
+-------------------+
         |
   LogicalPlan node tree
   (LogicalGet, LogicalFilter, LogicalProjection,
    LogicalJoin, LogicalAggregate,
    LogicalInsert, LogicalDelete, LogicalUpdate,
    LogicalCreateTable, LogicalDropTable)
         |
         v
     Optimizer (next stage)
```

**BindContext lifetime**: created fresh for each statement; destroyed after binding completes.

## 5. Core Data Structures (C++)

```cpp
// src/planner/bind_context.hpp
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "common/types.hpp"

namespace cppcoldb {

// Maps column name (and optionally "table.column") to a resolved column binding.
struct ColumnBinding {
    size_t table_idx;   // index in the from-clause table list (0 for single-table queries)
    size_t column_idx;  // index within the table's column list
    TypeId type;        // resolved SQL type
    std::string column_name;
};

class BindContext {
public:
    // Register all columns of a table into the context.
    void AddTable(size_t table_idx, const std::string& alias,
                  const std::vector<std::string>& col_names,
                  const std::vector<TypeId>& col_types);

    // Look up an unqualified column name; throws BindError on ambiguity or not found.
    const ColumnBinding& ResolveColumn(const std::string& col_name) const;

    // Look up a qualified "table.column" reference.
    const ColumnBinding& ResolveQualified(const std::string& table,
                                          const std::string& col) const;

private:
    // key: column_name (may collide across tables → ambiguity error)
    std::unordered_map<std::string, std::vector<ColumnBinding>> columns_;
    // key: "table.column"
    std::unordered_map<std::string, ColumnBinding> qualified_;
};

} // namespace cppcoldb
```

```cpp
// src/planner/logical_plan/logical_plan.hpp
#pragma once
#include <memory>
#include <vector>
#include <string>
#include "common/types.hpp"

namespace cppcoldb {

// Resolved expression nodes in the logical plan.
struct LogicalExpr {
    enum class Kind {
        BOUND_COLUMN_REF,   // resolved to (table_idx, column_idx, type)
        INTEGER_LIT,
        FLOAT_LIT,
        STRING_LIT,
        BOOL_LIT,
        NULL_LIT,
        BINARY_OP,
        UNARY_OP,
        CAST,               // implicit type cast
        AGGREGATE,          // COUNT/SUM/MIN/MAX/AVG
    };
    Kind    kind;
    TypeId  result_type;    // type this expression evaluates to
    virtual ~LogicalExpr() = default;
};

struct BoundColumnRef : LogicalExpr {
    size_t table_idx;
    size_t column_idx;
    std::string name;       // for display/debug
};

struct LogicalBinaryOp : LogicalExpr {
    std::string op;
    std::unique_ptr<LogicalExpr> left;
    std::unique_ptr<LogicalExpr> right;
};

struct LogicalCast : LogicalExpr {
    TypeId                       from_type;
    std::unique_ptr<LogicalExpr> child;
};

struct LogicalAggrExpr : LogicalExpr {
    enum class AggFunc { COUNT, SUM, MIN, MAX, AVG };
    AggFunc                      func;
    bool                         is_star; // COUNT(*)
    std::unique_ptr<LogicalExpr> arg;
};

// Base class for all logical plan nodes.
struct LogicalPlan {
    enum class Type {
        GET,         // table scan
        FILTER,
        PROJECTION,
        JOIN,
        AGGREGATE,
        INSERT,
        DELETE,
        UPDATE,
        CREATE_TABLE,
        DROP_TABLE,
        EXPLAIN,
    };
    Type node_type;
    std::vector<std::unique_ptr<LogicalPlan>> children; // 0, 1, or 2 children
    virtual ~LogicalPlan() = default;
};

struct LogicalGet : LogicalPlan {
    std::string  schema_name;
    std::string  table_name;
    std::vector<size_t>  column_ids;    // indices of columns to read
    std::vector<TypeId>  column_types;
    std::vector<std::string> column_names;
};

struct LogicalFilter : LogicalPlan {
    std::unique_ptr<LogicalExpr> predicate;
};

struct LogicalProjection : LogicalPlan {
    std::vector<std::unique_ptr<LogicalExpr>> exprs;
    std::vector<std::string>                  output_names;
};

struct LogicalJoin : LogicalPlan {
    enum class JoinType { INNER, LEFT, RIGHT, CROSS };
    JoinType                     join_type;
    std::unique_ptr<LogicalExpr> condition; // nullptr for CROSS JOIN
};

struct LogicalAggregate : LogicalPlan {
    std::vector<std::unique_ptr<LogicalExpr>> group_keys;
    std::vector<std::unique_ptr<LogicalAggrExpr>> aggregates;
    std::vector<std::string>                  output_names;
};

struct LogicalInsert : LogicalPlan {
    std::string              table_name;
    std::string              schema_name;
    std::vector<size_t>      column_ids;  // target column indices
    // rows are expressed as a LogicalPlan child (values source)
};

struct LogicalDelete : LogicalPlan {
    std::string table_name;
    std::string schema_name;
};

struct LogicalUpdate : LogicalPlan {
    std::string                               table_name;
    std::string                               schema_name;
    std::vector<size_t>                       column_ids;
    std::vector<std::unique_ptr<LogicalExpr>> new_values;
};

struct LogicalCreateTable : LogicalPlan {
    std::string              schema_name;
    std::string              table_name;
    std::vector<std::string> column_names;
    std::vector<TypeId>      column_types;
    std::vector<bool>        not_null_flags;
    bool                     if_not_exists;
};

struct LogicalDropTable : LogicalPlan {
    std::string schema_name;
    std::string table_name;
    bool        if_exists;
};

} // namespace cppcoldb
```

## 6. Public Interfaces

```cpp
// src/planner/binder.hpp
namespace cppcoldb {

class Catalog;
class Transaction;

class Binder {
public:
    Binder(Catalog& catalog, Transaction& tx);

    // Bind a parsed statement and return a logical plan.
    // Throws BindError on semantic violations.
    std::unique_ptr<LogicalPlan> Bind(const ParsedStatement& stmt);

private:
    std::unique_ptr<LogicalPlan> BindSelect(const SelectStatement& s);
    std::unique_ptr<LogicalPlan> BindInsert(const InsertStatement& s);
    std::unique_ptr<LogicalPlan> BindUpdate(const UpdateStatement& s);
    std::unique_ptr<LogicalPlan> BindDelete(const DeleteStatement& s);
    std::unique_ptr<LogicalPlan> BindCreateTable(const CreateTableStatement& s);
    std::unique_ptr<LogicalPlan> BindDropTable(const DropTableStatement& s);
    std::unique_ptr<LogicalPlan> BindExplain(const ExplainStatement& s);

    // Expression binding
    std::unique_ptr<LogicalExpr> BindExpr(const Expr& e, const BindContext& ctx);
    std::unique_ptr<LogicalExpr> BindBinaryOp(const BinaryOpExpr& e, const BindContext& ctx);
    std::unique_ptr<LogicalExpr> BindAggregate(const FunctionCallExpr& e, const BindContext& ctx);

    // Type promotion: wrap expr in LogicalCast if from_type != to_type.
    std::unique_ptr<LogicalExpr> Coerce(std::unique_ptr<LogicalExpr> expr, TypeId to_type);

    Catalog&     catalog_;
    Transaction& tx_;
};

} // namespace cppcoldb

// src/planner/logical_planner.hpp
namespace cppcoldb {
class LogicalPlanner {
public:
    LogicalPlanner(Catalog& catalog, Transaction& tx);
    std::unique_ptr<LogicalPlan> Plan(const ParsedStatement& stmt);
private:
    Binder binder_;
};
} // namespace cppcoldb
```

## 7. Internal Algorithms

### BindContext population
```
// Called once per FROM-clause table before any expressions are bound.
ctx.AddTable(table_idx=0, alias="orders", col_names, col_types):
  for i, (name, type) in enumerate(zip(col_names, col_types)):
    binding = ColumnBinding{table_idx=0, column_idx=i, type, name}
    columns_[name].append(binding)           // unqualified: "id", "name", ...
    qualified_["orders." + name] = binding   // qualified: "orders.id", ...

ResolveColumn("id"):
  matches = columns_["id"]
  if matches.empty():  throw BindError("column 'id' not found")
  if matches.size() > 1: throw BindError("column 'id' is ambiguous")
  return matches[0]

ResolveQualified("orders", "id"):
  key = "orders.id"
  if qualified_.count(key) == 0: throw BindError("column 'orders.id' not found")
  return qualified_[key]
```

### SELECT * expansion
```
// SelectStatement.select_list contains a single StarExpr when user wrote SELECT *.
// The binder must expand it into one BoundColumnRef per column in the table.

ExpandStar(table_entry, ctx) -> vector<unique_ptr<LogicalExpr>>:
  result = []
  for i, (name, type) in enumerate(zip(table_entry.col_names, table_entry.col_types)):
    ref = BoundColumnRef{table_idx=0, column_idx=i, result_type=type, name=name}
    result.append(ref)
  return result

// In BindSelect step 5, before building the projection:
expanded_select = []
for expr in s.select_list:
  if expr.kind == STAR:
    expanded_select += ExpandStar(table_entry, ctx)   // replaces * with N columns
  else:
    expanded_select.append(BindExpr(expr, ctx))
```

### BindSelect (complete)
```
BindSelect(SelectStatement s):
  // Step 1: resolve FROM table
  schema = s.from_schema.empty() ? "main" : s.from_schema
  table_entry = Catalog::GetEntry(schema, s.from_table, tx)
  if not found: throw BindError("table '" + s.from_table + "' not found")

  // Step 2: populate BindContext
  ctx = BindContext()
  alias = s.from_table  // use table name as alias (no AS support yet)
  ctx.AddTable(0, alias, table_entry.col_names, table_entry.col_types)

  // Step 3: build LogicalGet (starts with all columns; pruned later by optimizer)
  get_node = LogicalGet{schema, s.from_table, column_ids=[0..ncols-1],
                        column_types, column_names}

  // Step 4: optional WHERE → LogicalFilter
  above_get = get_node
  if s.where_clause:
    predicate = BindExpr(*s.where_clause, ctx)
    if predicate.result_type != BOOLEAN:
      throw BindError("WHERE clause must be BOOLEAN, got " + type_name(predicate.result_type))
    filter_node = LogicalFilter{predicate}
    filter_node.children = [get_node]
    above_get = filter_node

  // Step 5: expand SELECT * and bind select list
  expanded = []
  for expr in s.select_list:
    if expr.kind == STAR:
      expanded += ExpandStar(table_entry, ctx)
    else:
      expanded.append(BindExpr(*expr, ctx))

  // Step 6: optional GROUP BY + aggregates
  has_aggregates = any(e.kind == AGGREGATE for e in expanded)
  if has_aggregates or not s.group_by.empty():
    // Validate: non-aggregate select-list items must appear in GROUP BY
    group_keys = [BindExpr(*e, ctx) for e in s.group_by]
    agg_exprs  = []
    proj_exprs = []  // will be LogicalProjection over the aggregate output
    for e in expanded:
      if e.kind == AGGREGATE:
        agg_exprs.append(e as LogicalAggrExpr)
      else:
        // Must be in group_by; validate by checking it resolves to a group key
        proj_exprs.append(e)
    agg_node = LogicalAggregate{group_keys, agg_exprs, output_names}
    agg_node.children = [above_get]
    top = agg_node
    // If HAVING clause present, wrap agg_node in a LogicalFilter
    if s.having_clause:
      having_pred = BindExpr(*s.having_clause, agg_output_ctx)
      having_node = LogicalFilter{having_pred}
      having_node.children = [agg_node]
      top = having_node
  else:
    proj_node = LogicalProjection{expanded, output_names}
    proj_node.children = [above_get]
    top = proj_node

  // Step 7: optional ORDER BY
  // Stored in LogicalProjection or a new LogicalSort node (if implementing ORDER BY)
  // Simple approach: attach order_by bindings as metadata on the top node
  if not s.order_by.empty():
    sort_keys = [BindExpr(*e, ctx) for e in s.order_by]
    sort_node = LogicalSort{sort_keys, s.order_asc}
    sort_node.children = [top]
    top = sort_node

  // Step 8: optional LIMIT
  if s.limit.has_value():
    if s.limit.value() < 0: throw BindError("LIMIT must be non-negative")
    limit_node = LogicalLimit{s.limit.value(), s.offset.value_or(0)}
    limit_node.children = [top]
    top = limit_node

  return top
  Time: O(cols + exprs + group_keys)
```

### BindInsert (column count and type validation)
```
BindInsert(InsertStatement s):
  table_entry = Catalog::GetEntry(schema, s.table_name, tx)
  if not found: throw BindError("table not found")

  // Resolve target column indices
  target_col_ids = []
  if s.column_names.empty():
    // No column list → all columns in definition order
    target_col_ids = [0, 1, ..., ncols-1]
  else:
    for name in s.column_names:
      idx = table_entry.FindColumn(name)
      if idx == NOT_FOUND: throw BindError("column '" + name + "' not found in table")
      target_col_ids.append(idx)

  // Validate each row of values
  for row_idx, row in enumerate(s.values):
    if row.size() != target_col_ids.size():
      throw BindError("row " + row_idx + ": expected " + target_col_ids.size()
                      + " values, got " + row.size())
    for i, expr in enumerate(row):
      bound = BindExpr(*expr, empty_ctx)  // literals only; no column refs
      expected_type = table_entry.col_types[target_col_ids[i]]
      if bound.result_type != expected_type:
        // Try implicit cast
        if CanCoerce(bound.result_type, expected_type):
          row[i] = Coerce(bound, expected_type)
        else:
          throw BindError("column '" + name + "': cannot assign "
                          + type_name(bound.result_type) + " to "
                          + type_name(expected_type))

  return LogicalInsert{schema, table, target_col_ids, values_plan}
  Time: O(rows * cols)
```

### BindExpr (expression binding)
```
BindExpr(expr, ctx):
  switch expr.kind:
    COLUMN_REF:
      if expr.table_name.empty():
        binding = ctx.ResolveColumn(expr.column_name)
      else:
        binding = ctx.ResolveQualified(expr.table_name, expr.column_name)
      return BoundColumnRef{binding.table_idx, binding.column_idx,
                            result_type=binding.type, name=binding.column_name}
    INTEGER_LIT: return LogicalLit{value=e.value, result_type=INT64}
    FLOAT_LIT:   return LogicalLit{value=e.value, result_type=FLOAT64}
    STRING_LIT:  return LogicalLit{value=e.value, result_type=VARCHAR}
    BOOL_LIT:    return LogicalLit{value=e.value, result_type=BOOLEAN}
    NULL_LIT:    return LogicalLit{is_null=true,  result_type=INVALID}
    BINARY_OP:
      left  = BindExpr(*e.left,  ctx)
      right = BindExpr(*e.right, ctx)
      [left, right, result_type] = InferBinaryType(e.op, left, right)
      return LogicalBinaryOp{e.op, left, right, result_type}
    UNARY_OP:
      child = BindExpr(*e.operand, ctx)
      if e.op == "NOT" and child.result_type != BOOLEAN:
        throw BindError("NOT requires BOOLEAN operand")
      if e.op == "-" and not IsNumeric(child.result_type):
        throw BindError("unary minus requires numeric operand")
      return LogicalUnaryOp{e.op, child, result_type=child.result_type}
    FUNCTION_CALL:
      return BindAggregate(e, ctx)
  Time: O(expr_tree_nodes)
```

### Type inference for binary expressions
```
InferBinaryType(op, left, right) -> (left', right', result_type):
  // Arithmetic operators
  if op in {"+", "-", "*", "/"}:
    if IsInteger(left.result_type) and IsInteger(right.result_type):
      return (left, right, INT64)
    if IsNumeric(left.result_type) and IsNumeric(right.result_type):
      // Promote both sides to FLOAT64
      if left.result_type  != FLOAT64: left  = Coerce(left,  FLOAT64)
      if right.result_type != FLOAT64: right = Coerce(right, FLOAT64)
      return (left, right, FLOAT64)
    throw BindError("operator '" + op + "' requires numeric operands, got "
                    + type_name(left.result_type) + " and "
                    + type_name(right.result_type))

  // Comparison operators
  if op in {"=", "<>", "<", ">", "<=", ">="}:
    if left.result_type == right.result_type:
      return (left, right, BOOLEAN)
    if IsNumeric(left.result_type) and IsNumeric(right.result_type):
      // Promote both to FLOAT64 for mixed int/float comparison
      if left.result_type  != FLOAT64: left  = Coerce(left,  FLOAT64)
      if right.result_type != FLOAT64: right = Coerce(right, FLOAT64)
      return (left, right, BOOLEAN)
    throw BindError("cannot compare " + type_name(left.result_type)
                    + " with " + type_name(right.result_type))

  // Logical operators
  if op in {"AND", "OR"}:
    if left.result_type != BOOLEAN:
      throw BindError("AND/OR left operand must be BOOLEAN")
    if right.result_type != BOOLEAN:
      throw BindError("AND/OR right operand must be BOOLEAN")
    return (left, right, BOOLEAN)

// Helpers
IsInteger(t): t in {INT8, INT16, INT32, INT64}
IsNumeric(t): t in {INT8, INT16, INT32, INT64, FLOAT32, FLOAT64}
Coerce(expr, to_type): wrap expr in LogicalCast{from_type=expr.result_type,
                                                result_type=to_type, child=expr}
CanCoerce(from, to): IsNumeric(from) and IsNumeric(to)
                     or (from == INT64 and to == FLOAT64)  // etc.
```

## 8. Persistence Model

Not applicable. The Binder is a pure in-memory transformation pass.

## 9. Concurrency Model

The `Binder` is created fresh per query and is never shared across threads. The `Catalog` it reads from is thread-safe for reads (protected by its own lock). No additional locking needed inside the Binder.

## 10. Configuration

No runtime configuration. All behavior is determined by the SQL semantics and catalog contents.

## 11. Testing Strategy

- `TestBindSimpleSelect`: bind `SELECT id FROM orders` → LogicalGet with correct column_ids
- `TestBindWhereClause`: `SELECT * FROM t WHERE a > 5` → LogicalFilter wrapping LogicalGet
- `TestBindTableNotFound`: `SELECT * FROM nonexistent` → throws BindError
- `TestBindColumnNotFound`: `SELECT zzz FROM t` → throws BindError
- `TestBindAmbiguousColumn`: two tables in context with same column name → throws BindError
- `TestBindTypeMismatch`: `SELECT 'hello' + 1` → throws BindError
- `TestBindNumericPromotion`: `SELECT int_col + float_col` → left side wrapped in LogicalCast{FLOAT64}
- `TestBindAggrCount`: `SELECT COUNT(*) FROM t` → LogicalAggregate with COUNT(*) aggregate
- `TestBindGroupBy`: `SELECT dept, SUM(salary) FROM t GROUP BY dept` → correct grouping
- `TestBindInsert`: `INSERT INTO t (a,b) VALUES (1,'x')` → LogicalInsert with column_ids
- `TestBindCreateTable`: `CREATE TABLE t (id INT64 PRIMARY KEY)` → LogicalCreateTable
- `TestBindDropTableIfExists`: `DROP TABLE IF EXISTS t` → LogicalDropTable{if_exists=true}
- `TestBindExplainAnalyze`: `EXPLAIN ANALYZE SELECT 1` → LogicalExplain wrapping inner plan
- `TestBindMVCCVisibility`: table created in a different (committed) transaction is visible

## 12. Open Questions

- Multi-table FROM / explicit JOIN syntax: the initial plan handles single-table FROM only; JOIN binding will be added when join execution is implemented.
- Schema-qualified identifiers in expressions (e.g., `schema.table.column`): deferred.
