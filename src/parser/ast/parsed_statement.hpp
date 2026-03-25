#pragma once
#include <memory>
#include <vector>
#include <string>
#include <optional>
#include "common/types.hpp"

namespace cppcoldb {

// ---------------------------------------------------------------------------
// Expression nodes
// ---------------------------------------------------------------------------

struct Expr {
    enum class Kind {
        COLUMN_REF,
        INTEGER_LIT,
        FLOAT_LIT,
        STRING_LIT,
        BOOL_LIT,
        NULL_LIT,
        BINARY_OP,
        UNARY_OP,
        FUNCTION_CALL,
        STAR,
    };
    Kind kind;
    virtual ~Expr() = default;
};

struct ColumnRefExpr : Expr {
    ColumnRefExpr() { kind = Kind::COLUMN_REF; }
    std::string table_name;  // empty if unqualified
    std::string column_name;
};

struct IntegerLitExpr : Expr {
    IntegerLitExpr() { kind = Kind::INTEGER_LIT; }
    int64_t value = 0;
};

struct FloatLitExpr : Expr {
    FloatLitExpr() { kind = Kind::FLOAT_LIT; }
    double value = 0.0;
};

struct StringLitExpr : Expr {
    StringLitExpr() { kind = Kind::STRING_LIT; }
    std::string value;
};

struct BoolLitExpr : Expr {
    BoolLitExpr() { kind = Kind::BOOL_LIT; }
    bool value = false;
};

struct NullLitExpr : Expr {
    NullLitExpr() { kind = Kind::NULL_LIT; }
};

struct StarExpr : Expr {
    StarExpr() { kind = Kind::STAR; }
};

struct BinaryOpExpr : Expr {
    BinaryOpExpr() { kind = Kind::BINARY_OP; }
    std::string op; // "+", "-", "*", "/", "=", "<", ">", "<=", ">=", "<>", "AND", "OR"
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
};

struct UnaryOpExpr : Expr {
    UnaryOpExpr() { kind = Kind::UNARY_OP; }
    std::string op; // "NOT", "-"
    std::unique_ptr<Expr> operand;
};

struct FunctionCallExpr : Expr {
    FunctionCallExpr() { kind = Kind::FUNCTION_CALL; }
    std::string name;
    std::vector<std::unique_ptr<Expr>> args;
    bool is_star = false; // COUNT(*) special case
};

// ---------------------------------------------------------------------------
// Column definition (CREATE TABLE)
// ---------------------------------------------------------------------------

struct ColumnDef {
    std::string name;
    TypeId      type        = TypeId::INVALID;
    bool        not_null    = false;
    bool        primary_key = false;
};

// ---------------------------------------------------------------------------
// Statement nodes
// ---------------------------------------------------------------------------

struct ParsedStatement {
    enum class Type {
        SELECT,
        INSERT,
        UPDATE,
        DELETE,
        CREATE_TABLE,
        DROP_TABLE,
        ALTER_TABLE,
        BEGIN,
        COMMIT,
        ROLLBACK,
        EXPLAIN,
    };
    Type stmt_type;
    virtual ~ParsedStatement() = default;
};

struct JoinClause {
    std::string           table_name;
    std::string           schema_name; // empty → default "main"
    std::unique_ptr<Expr> condition;   // ON clause
};

struct SelectStatement : ParsedStatement {
    SelectStatement() { stmt_type = Type::SELECT; }
    bool                               distinct = false;
    std::vector<std::unique_ptr<Expr>> select_list;
    std::string                        from_table;
    std::string                        from_schema; // empty → default "main"
    std::vector<JoinClause>            joins;        // INNER JOIN clauses in order
    std::unique_ptr<Expr>              where_clause;
    std::vector<std::unique_ptr<Expr>> group_by;
    std::vector<std::unique_ptr<Expr>> order_by;
    std::vector<bool>                  order_asc; // parallel to order_by; true = ASC
    std::optional<int64_t>             limit;
    std::optional<int64_t>             offset;
};

struct InsertStatement : ParsedStatement {
    InsertStatement() { stmt_type = Type::INSERT; }
    std::string              table_name;
    std::string              schema_name;
    std::vector<std::string> column_names; // empty → all columns in table order
    std::vector<std::vector<std::unique_ptr<Expr>>> values; // one inner vec per row (VALUES path)
    std::unique_ptr<SelectStatement> select_stmt; // non-null for INSERT...SELECT
};

struct UpdateSetClause {
    std::string           column_name;
    std::unique_ptr<Expr> value_expr;
};

struct UpdateStatement : ParsedStatement {
    UpdateStatement() { stmt_type = Type::UPDATE; }
    std::string                  table_name;
    std::string                  schema_name;
    std::vector<UpdateSetClause> set_clauses;
    std::unique_ptr<Expr>        where_clause;
};

struct DeleteStatement : ParsedStatement {
    DeleteStatement() { stmt_type = Type::DELETE; }
    std::string           table_name;
    std::string           schema_name;
    std::unique_ptr<Expr> where_clause;
};

struct CreateTableStatement : ParsedStatement {
    CreateTableStatement() { stmt_type = Type::CREATE_TABLE; }
    std::string            schema_name;
    std::string            table_name;
    std::vector<ColumnDef> columns;
    bool                   if_not_exists = false;

    // Partition specification (optional).
    enum class PartitionKind { NONE, RANGE, HASH, LIST };
    PartitionKind                partition_kind = PartitionKind::NONE;
    std::vector<std::string>     partition_cols;          // 1+ key column names
    uint32_t                     hash_partition_count = 0; // HASH only

    // RANGE: range_bounds[i][k] = k-th key column expression for the i-th split-point.
    // Single-key: range_bounds[i] is a 1-element vector.
    std::vector<std::vector<std::unique_ptr<Expr>>> range_bounds;

    // LIST: list_values[i][j][k] = k-th key column expression for the j-th tuple of partition i.
    // Single-key: list_values[i][j] is a 1-element vector.
    std::vector<std::vector<std::vector<std::unique_ptr<Expr>>>> list_values;
};

struct DropTableStatement : ParsedStatement {
    DropTableStatement() { stmt_type = Type::DROP_TABLE; }
    std::string schema_name;
    std::string table_name;
    bool        if_exists = false;
};

struct AlterTableStatement : ParsedStatement {
    AlterTableStatement() { stmt_type = Type::ALTER_TABLE; }
    enum class AlterKind { DROP_PARTITION, ADD_PARTITION };
    AlterKind   kind        = AlterKind::DROP_PARTITION;
    std::string schema_name;
    std::string table_name;
    // DROP PARTITION: partition index to remove.
    uint32_t    partition_idx = 0;
    // ADD PARTITION (RANGE): new upper bound tuple (one expr per key column).
    std::vector<std::unique_ptr<Expr>> new_range_bound;
    // ADD PARTITION (LIST): new value tuples for the new partition; [j][k] = j-th tuple, k-th col.
    std::vector<std::vector<std::unique_ptr<Expr>>> new_list_values;
};

struct TransactionStatement : ParsedStatement {
    // stmt_type encodes BEGIN / COMMIT / ROLLBACK
};

struct ExplainStatement : ParsedStatement {
    ExplainStatement() { stmt_type = Type::EXPLAIN; }
    bool                             analyze = false;
    std::unique_ptr<ParsedStatement> inner;
};

} // namespace cppcoldb
