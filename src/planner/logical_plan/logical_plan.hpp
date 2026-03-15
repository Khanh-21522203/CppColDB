#pragma once
#include <memory>
#include <string>
#include <vector>
#include "common/types.hpp"
#include "catalog/catalog_entry.hpp"

namespace cppcoldb {

// ---------------------------------------------------------------------------
// LogicalExpr hierarchy
// ---------------------------------------------------------------------------

struct LogicalExpr {
    enum class Kind {
        BOUND_COLUMN_REF,
        LITERAL,
        BINARY_OP,
        UNARY_OP,
        CAST,
        AGGREGATE,
    };
    Kind   kind;
    TypeId result_type = TypeId::INVALID;
    virtual ~LogicalExpr() = default;
};

// A reference to a column by its 0-based position in the input chunk (after
// any column pruning / remapping), NOT the original table column index.
struct BoundColumnRef : LogicalExpr {
    BoundColumnRef() { kind = Kind::BOUND_COLUMN_REF; }
    size_t      column_idx = 0;  // chunk-position index
    std::string name;            // debug/display only
};

// A scalar literal value. When is_null==true the value is NULL regardless of
// the contents of `value`.
struct LogicalLit : LogicalExpr {
    LogicalLit() { kind = Kind::LITERAL; }
    Value value;
    bool  is_null = false;
};

struct LogicalBinaryOp : LogicalExpr {
    LogicalBinaryOp() { kind = Kind::BINARY_OP; }
    // op: "+","-","*","/","=","<",">","<=",">=","<>","AND","OR"
    std::string                  op;
    std::unique_ptr<LogicalExpr> left;
    std::unique_ptr<LogicalExpr> right;
};

struct LogicalUnaryOp : LogicalExpr {
    LogicalUnaryOp() { kind = Kind::UNARY_OP; }
    // op: "NOT", "-"
    std::string                  op;
    std::unique_ptr<LogicalExpr> child;
};

struct LogicalCast : LogicalExpr {
    LogicalCast() { kind = Kind::CAST; }
    TypeId                       from_type = TypeId::INVALID;
    std::unique_ptr<LogicalExpr> child;
};

struct LogicalAggrExpr : LogicalExpr {
    LogicalAggrExpr() { kind = Kind::AGGREGATE; }
    enum class AggFunc { COUNT, SUM, MIN, MAX, AVG };
    AggFunc                      func;
    bool                         is_star = false;  // true for COUNT(*)
    std::unique_ptr<LogicalExpr> arg;               // nullptr when is_star==true
};

// Deep-clone an expression tree.
std::unique_ptr<LogicalExpr> CloneExpr(const LogicalExpr& e);

// ---------------------------------------------------------------------------
// LogicalPlan hierarchy
// ---------------------------------------------------------------------------

struct LogicalPlan {
    enum class Type {
        GET,
        FILTER,
        PROJECTION,
        LIMIT,
        SORT,
        AGGREGATE,
        INSERT,
        DELETE,
        UPDATE,
        CREATE_TABLE,
        DROP_TABLE,
    };
    Type node_type;
    std::vector<std::unique_ptr<LogicalPlan>> children;
    // Output schema — set by the binder.
    std::vector<TypeId>      output_types;
    std::vector<std::string> output_names;
    virtual ~LogicalPlan() = default;
};

// Sequential scan of a base table; reads only the listed column positions.
struct LogicalGet : LogicalPlan {
    LogicalGet() { node_type = Type::GET; }
    std::string              schema_name;
    std::string              table_name;
    std::vector<size_t>      column_ids;          // which columns to read
    uint64_t                 catalog_row_count = 0;
    // Zone-map / predicate push-down hints for the optimizer.
    std::vector<std::unique_ptr<LogicalExpr>> pushed_filters;
};

// Row-level predicate applied to the output of its single child.
struct LogicalFilter : LogicalPlan {
    LogicalFilter() { node_type = Type::FILTER; }
    std::unique_ptr<LogicalExpr> predicate;
};

// Computes a fixed list of expressions over the child's output.
struct LogicalProjection : LogicalPlan {
    LogicalProjection() { node_type = Type::PROJECTION; }
    std::vector<std::unique_ptr<LogicalExpr>> exprs;
};

struct LogicalLimit : LogicalPlan {
    LogicalLimit() { node_type = Type::LIMIT; }
    int64_t limit  = -1;  // -1 means no limit
    int64_t offset =  0;
};

struct LogicalSort : LogicalPlan {
    LogicalSort() { node_type = Type::SORT; }
    std::vector<std::unique_ptr<LogicalExpr>> sort_keys;
    std::vector<bool>                         ascending;  // parallel to sort_keys
};

// Hash aggregate: group_exprs may be empty (scalar aggregate).
struct LogicalAggregate : LogicalPlan {
    LogicalAggregate() { node_type = Type::AGGREGATE; }
    std::vector<std::unique_ptr<LogicalExpr>> group_exprs;
    std::vector<std::unique_ptr<LogicalExpr>> aggr_exprs;
};

// children[0] is a LogicalProjection that produces the rows to insert.
struct LogicalInsert : LogicalPlan {
    LogicalInsert() { node_type = Type::INSERT; }
    std::string         schema_name;
    std::string         table_name;
    std::vector<size_t> column_ids;  // target column indices in table order
};

// children[0] is LogicalFilter(LogicalGet) — identifies rows to delete.
struct LogicalDelete : LogicalPlan {
    LogicalDelete() { node_type = Type::DELETE; }
    std::string schema_name;
    std::string table_name;
};

// children[0] is LogicalFilter(LogicalGet) — identifies rows to update.
struct LogicalUpdate : LogicalPlan {
    LogicalUpdate() { node_type = Type::UPDATE; }
    std::string                               schema_name;
    std::string                               table_name;
    std::vector<size_t>                       update_col_ids;
    std::vector<std::unique_ptr<LogicalExpr>> update_exprs;
};

struct LogicalCreateTable : LogicalPlan {
    LogicalCreateTable() { node_type = Type::CREATE_TABLE; }
    std::string                   schema_name;
    std::string                   table_name;
    std::vector<ColumnDefinition> columns;
    bool                          if_not_exists = false;
};

struct LogicalDropTable : LogicalPlan {
    LogicalDropTable() { node_type = Type::DROP_TABLE; }
    std::string schema_name;
    std::string table_name;
    bool        if_exists = false;
};

} // namespace cppcoldb
