#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cppcoldb/common/types/data_chunk.hpp"
#include "cppcoldb/common/types/type_id.hpp"
#include "cppcoldb/common/types/value.hpp"

namespace cppcoldb::sql::planner {

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
    Kind           kind;
    common::TypeId result_type = common::TypeId::INVALID;
    virtual ~LogicalExpr() = default;
};

// A reference to a column by its 0-based position in the input chunk (after
// any column pruning / remapping), NOT the original table column index.
struct BoundColumnRef : LogicalExpr {
    BoundColumnRef() { kind = Kind::BOUND_COLUMN_REF; }
    std::size_t column_idx = 0; // chunk-position index
    std::string name;           // debug/display only
};

// A scalar literal value. When is_null==true the value is NULL regardless of
// the contents of `value`.
struct LogicalLit : LogicalExpr {
    LogicalLit() { kind = Kind::LITERAL; }
    common::Value value;
    bool          is_null = false;
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
    common::TypeId               from_type = common::TypeId::INVALID;
    std::unique_ptr<LogicalExpr> child;
};

struct LogicalAggrExpr : LogicalExpr {
    LogicalAggrExpr() { kind = Kind::AGGREGATE; }
    enum class AggFunc { COUNT, SUM, MIN, MAX, AVG };
    AggFunc                      func = AggFunc::COUNT;
    bool                         is_star = false; // true for COUNT(*)
    std::unique_ptr<LogicalExpr> arg;             // nullptr when is_star==true
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
        JOIN,
        INSERT,
        DELETE,
        UPDATE,
        CREATE_TABLE,
        DROP_TABLE,
        ALTER_TABLE,
    };
    Type node_type;
    std::vector<std::unique_ptr<LogicalPlan>> children;
    // Output schema -- set by the binder.
    std::vector<common::TypeId> output_types;
    std::vector<std::string>    output_names;
    virtual ~LogicalPlan() = default;
};

// Sequential scan of a base table; reads only the listed column positions.
struct LogicalGet : LogicalPlan {
    LogicalGet() { node_type = Type::GET; }
    std::string               schema_name;
    std::string               table_name;
    std::vector<std::size_t>  column_ids; // which columns to read
    std::uint64_t             catalog_row_count = 0;
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
    std::int64_t limit  = -1; // -1 means no limit
    std::int64_t offset =  0;
};

struct LogicalSort : LogicalPlan {
    LogicalSort() { node_type = Type::SORT; }
    std::vector<std::unique_ptr<LogicalExpr>> sort_keys;
    std::vector<bool>                         ascending; // parallel to sort_keys
};

// Hash aggregate: group_exprs may be empty (scalar aggregate).
struct LogicalAggregate : LogicalPlan {
    LogicalAggregate() { node_type = Type::AGGREGATE; }
    std::vector<std::unique_ptr<LogicalExpr>> group_exprs;
    std::vector<std::unique_ptr<LogicalExpr>> aggr_exprs;
};

// Two-table INNER JOIN:
//   children[0] = left (probe) subtree
//   children[1] = right (build) subtree
//   output_types/names = left_types + right_types (set by binder)
struct LogicalJoin : LogicalPlan {
    LogicalJoin() { node_type = Type::JOIN; }
    std::unique_ptr<LogicalExpr> condition; // full ON-clause expression
    std::vector<std::size_t>     left_key_col_ids;  // equi-join key positions in left output
    std::vector<std::size_t>     right_key_col_ids; // equi-join key positions in right output
};

// children[0] is a LogicalProjection that produces the rows to insert.
struct LogicalInsert : LogicalPlan {
    LogicalInsert() { node_type = Type::INSERT; }
    std::string               schema_name;
    std::string               table_name;
    std::vector<std::size_t>  column_ids; // target column indices in table order
    common::DataChunk         rows;       // pre-evaluated literal values (VALUES path)
    bool                      has_select_source = false; // children[0] = SELECT plan
};

// children[0] is LogicalFilter(LogicalGet) -- identifies rows to delete.
struct LogicalDelete : LogicalPlan {
    LogicalDelete() { node_type = Type::DELETE; }
    std::string schema_name;
    std::string table_name;
};

// children[0] is LogicalFilter(LogicalGet) -- identifies rows to update.
struct LogicalUpdate : LogicalPlan {
    LogicalUpdate() { node_type = Type::UPDATE; }
    std::string                               schema_name;
    std::string                               table_name;
    std::vector<std::size_t>                  update_col_ids;
    std::vector<std::unique_ptr<LogicalExpr>> update_exprs;
};

// Column definition for CREATE TABLE, mirrored from the parser AST after
// binding. Deliberately independent of the catalog's own column-definition
// type so that sql/ has no compile-time dependency on engine::catalog.
struct LogicalColumnDef {
    std::string    name;
    common::TypeId type        = common::TypeId::INVALID;
    bool           not_null    = false;
    bool           primary_key = false;
};

struct LogicalCreateTable : LogicalPlan {
    LogicalCreateTable() { node_type = Type::CREATE_TABLE; }
    std::string                   schema_name;
    std::string                   table_name;
    std::vector<LogicalColumnDef> columns;
    bool                          if_not_exists = false;

    // Partition specification (optional); mirrors parser::CreateTableStatement.
    enum class PartitionKind { NONE, RANGE, HASH, LIST };
    PartitionKind             partition_kind = PartitionKind::NONE;
    std::vector<std::string> partition_cols;
    std::uint32_t             hash_partition_count = 0;

    // RANGE: range_bounds[i][k] = k-th key column expression for the i-th split-point.
    std::vector<std::vector<std::unique_ptr<LogicalExpr>>> range_bounds;
    // LIST: list_values[i][j][k] = k-th key column expression for the j-th tuple of partition i.
    std::vector<std::vector<std::vector<std::unique_ptr<LogicalExpr>>>> list_values;
};

struct LogicalDropTable : LogicalPlan {
    LogicalDropTable() { node_type = Type::DROP_TABLE; }
    std::string schema_name;
    std::string table_name;
    bool        if_exists = false;
};

struct LogicalAlterTable : LogicalPlan {
    LogicalAlterTable() { node_type = Type::ALTER_TABLE; }
    enum class AlterKind { DROP_PARTITION, ADD_PARTITION };
    AlterKind     kind          = AlterKind::DROP_PARTITION;
    std::string   schema_name;
    std::string   table_name;
    std::uint32_t partition_idx = 0; // DROP
    // ADD PARTITION (RANGE): new upper bound tuple (one expr per key column).
    std::vector<std::unique_ptr<LogicalExpr>> new_range_bound;
    // ADD PARTITION (LIST): new value tuples for the new partition; [j][k] = j-th tuple, k-th col.
    std::vector<std::vector<std::unique_ptr<LogicalExpr>>> new_list_values;
};

} // namespace cppcoldb::sql::planner
