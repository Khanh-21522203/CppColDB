#include "planner/binder.hpp"
#include "catalog/catalog.hpp"
#include "catalog/catalog_entry.hpp"
#include "transaction/transaction.hpp"
#include "common/exception.hpp"
#include "storage/column/row_group.hpp"
#include <stdexcept>

namespace cppcoldb {

Binder::Binder(Catalog& catalog, Transaction& tx)
    : catalog_(catalog), tx_(tx) {}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

std::unique_ptr<LogicalPlan> Binder::Bind(const ParsedStatement& stmt) {
    switch (stmt.stmt_type) {
        case ParsedStatement::Type::SELECT:
            return BindSelect(static_cast<const SelectStatement&>(stmt));
        case ParsedStatement::Type::INSERT:
            return BindInsert(static_cast<const InsertStatement&>(stmt));
        case ParsedStatement::Type::UPDATE:
            return BindUpdate(static_cast<const UpdateStatement&>(stmt));
        case ParsedStatement::Type::DELETE:
            return BindDelete(static_cast<const DeleteStatement&>(stmt));
        case ParsedStatement::Type::CREATE_TABLE:
            return BindCreateTable(static_cast<const CreateTableStatement&>(stmt));
        case ParsedStatement::Type::DROP_TABLE:
            return BindDropTable(static_cast<const DropTableStatement&>(stmt));
        default:
            throw BindError("Binder: unsupported statement type");
    }
}

// ---------------------------------------------------------------------------
// SELECT
// ---------------------------------------------------------------------------

std::unique_ptr<LogicalPlan> Binder::BindSelect(const SelectStatement& stmt) {
    if (!stmt.joins.empty()) {
        return BindSelectWithJoin(stmt);
    }

    // Step 1: resolve FROM table
    if (stmt.from_table.empty()) {
        throw BindError("SELECT requires a FROM clause");
    }
    const std::string schema_name = stmt.from_schema.empty()
                                  ? Catalog::DEFAULT_SCHEMA
                                  : stmt.from_schema;

    TableCatalogEntry* table = catalog_.GetTable(schema_name, stmt.from_table, tx_);
    if (!table) {
        throw BindError("table not found: " + stmt.from_table);
    }

    // Step 2: build BindContext
    BindContext ctx;
    ctx.AddTable(0, stmt.from_table, table->ColumnNames(), table->ColumnTypes());

    // Step 3: build LogicalGet
    auto get = std::make_unique<LogicalGet>();
    get->schema_name = schema_name;
    get->table_name  = stmt.from_table;
    for (const auto& rg : table->row_groups) {
        get->catalog_row_count += rg->RowCount();
    }
    for (size_t i = 0; i < table->ColumnCount(); ++i) {
        get->column_ids.push_back(i);
    }
    get->output_types = table->ColumnTypes();
    get->output_names = table->ColumnNames();

    // Step 4: bind WHERE clause
    std::unique_ptr<LogicalPlan> source_owned = std::move(get);
    if (stmt.where_clause) {
        auto pred = BindExpr(*stmt.where_clause, ctx);
        if (pred->result_type != TypeId::BOOLEAN) {
            throw BindError("WHERE clause must be boolean");
        }
        auto filter = std::make_unique<LogicalFilter>();
        filter->predicate    = std::move(pred);
        filter->output_types = table->ColumnTypes();
        filter->output_names = table->ColumnNames();
        filter->children.push_back(std::move(source_owned));
        source_owned = std::move(filter);
    }

    // Step 5: detect aggregation (GROUP BY or aggregate functions in SELECT list)
    bool has_agg = !stmt.group_by.empty();
    if (!has_agg) {
        for (const auto& e : stmt.select_list) {
            if (e && HasAggregateExpr(*e)) { has_agg = true; break; }
        }
    }

    if (has_agg) {
        source_owned = BindAggregateSelect(stmt, ctx, std::move(source_owned));
        // BindAggregateSelect returns a LogicalProjection on top of LogicalAggregate.
        // Skip the normal select-list binding below and jump to ORDER BY/LIMIT.
        std::unique_ptr<LogicalPlan> top_owned = std::move(source_owned);

        if (!stmt.order_by.empty()) {
            // Aggregate ORDER BY is bound against post-aggregate output columns.
            BindContext out_ctx;
            out_ctx.AddTable(0, "__result__", top_owned->output_names, top_owned->output_types);

            auto sort = std::make_unique<LogicalSort>();
            for (size_t i = 0; i < stmt.order_by.size(); ++i) {
                try {
                    sort->sort_keys.push_back(BindExpr(*stmt.order_by[i], out_ctx));
                } catch (const BindError&) {
                    if (stmt.order_by[i]->kind != Expr::Kind::COLUMN_REF) throw;
                    const auto& cref = static_cast<const ColumnRefExpr&>(*stmt.order_by[i]);
                    if (cref.table_name.empty()) throw;
                    ColumnRefExpr unqualified;
                    unqualified.column_name = cref.column_name;
                    sort->sort_keys.push_back(BindExpr(unqualified, out_ctx));
                }
                sort->ascending.push_back(stmt.order_asc[i]);
            }
            sort->output_types = top_owned->output_types;
            sort->output_names = top_owned->output_names;
            sort->children.push_back(std::move(top_owned));
            top_owned = std::move(sort);
        }
        if (stmt.limit.has_value()) {
            if (*stmt.limit < 0) throw BindError("LIMIT must be non-negative");
            auto lim = std::make_unique<LogicalLimit>();
            lim->limit        = *stmt.limit;
            lim->offset       = stmt.offset.value_or(0);
            lim->output_types = top_owned->output_types;
            lim->output_names = top_owned->output_names;
            lim->children.push_back(std::move(top_owned));
            top_owned = std::move(lim);
        }
        return top_owned;
    }

    // Step 5b: bind SELECT list (no aggregation)
    std::vector<std::unique_ptr<LogicalExpr>> proj_exprs;
    std::vector<std::string>                  proj_names;
    std::vector<TypeId>                       proj_types;

    for (const auto& expr_ptr : stmt.select_list) {
        if (expr_ptr->kind == Expr::Kind::STAR) {
            // Expand * into all columns
            auto expanded = ExpandStar(ctx);
            for (auto& e : expanded) {
                proj_names.push_back(static_cast<BoundColumnRef&>(*e).name);
                proj_types.push_back(e->result_type);
                proj_exprs.push_back(std::move(e));
            }
        } else {
            auto bound = BindExpr(*expr_ptr, ctx, /*allow_aggregates=*/true);
            // Determine output name
            std::string name;
            if (expr_ptr->kind == Expr::Kind::COLUMN_REF) {
                name = static_cast<const ColumnRefExpr&>(*expr_ptr).column_name;
            } else {
                name = "?col";
            }
            proj_names.push_back(name);
            proj_types.push_back(bound->result_type);
            proj_exprs.push_back(std::move(bound));
        }
    }

    // Step 6: ORDER BY — placed BEFORE projection so sort keys reference the full
    // table column space (all columns still available at this point).
    if (!stmt.order_by.empty()) {
        auto sort = std::make_unique<LogicalSort>();
        for (size_t i = 0; i < stmt.order_by.size(); ++i) {
            sort->sort_keys.push_back(BindExpr(*stmt.order_by[i], ctx));
            sort->ascending.push_back(stmt.order_asc[i]);
        }
        sort->output_types = source_owned->output_types;
        sort->output_names = source_owned->output_names;
        sort->children.push_back(std::move(source_owned));
        source_owned = std::move(sort);
    }

    // Step 7: build LogicalProjection (wraps sort, if any, so ORDER BY key columns
    // are available to the sort operator before they are projected away).
    auto proj = std::make_unique<LogicalProjection>();
    proj->exprs        = std::move(proj_exprs);
    proj->output_types = proj_types;
    proj->output_names = proj_names;
    proj->children.push_back(std::move(source_owned));

    std::unique_ptr<LogicalPlan> top_owned = std::move(proj);

    // Step 8: LIMIT / OFFSET
    if (stmt.limit.has_value()) {
        if (*stmt.limit < 0) {
            throw BindError("LIMIT must be non-negative");
        }
        auto lim = std::make_unique<LogicalLimit>();
        lim->limit        = *stmt.limit;
        lim->offset       = stmt.offset.value_or(0);
        lim->output_types = top_owned->output_types;
        lim->output_names = top_owned->output_names;
        lim->children.push_back(std::move(top_owned));
        top_owned = std::move(lim);
    }

    return top_owned;
}

// ---------------------------------------------------------------------------
// INSERT
// ---------------------------------------------------------------------------

std::unique_ptr<LogicalPlan> Binder::BindInsert(const InsertStatement& stmt) {
    const std::string schema_name = stmt.schema_name.empty()
                                  ? Catalog::DEFAULT_SCHEMA
                                  : stmt.schema_name;

    TableCatalogEntry* table = catalog_.GetTable(schema_name, stmt.table_name, tx_);
    if (!table) {
        throw BindError("table not found: " + stmt.table_name);
    }

    // Resolve target column indices
    std::vector<size_t> column_ids;
    if (stmt.column_names.empty()) {
        for (size_t i = 0; i < table->ColumnCount(); ++i) {
            column_ids.push_back(i);
        }
    } else {
        for (const auto& col_name : stmt.column_names) {
            int idx = table->FindColumn(col_name);
            if (idx < 0) {
                throw BindError("column not found in table: " + col_name);
            }
            column_ids.push_back(static_cast<size_t>(idx));
        }
    }

    // Build a DataChunk from the literal VALUES rows.
    BindContext empty_ctx; // no columns — values must be literals

    // Collect target column types.
    std::vector<TypeId> col_types;
    col_types.reserve(column_ids.size());
    for (size_t col_id : column_ids) {
        col_types.push_back(table->columns[col_id].type);
    }

    DataChunk rows;
    rows.Initialize(col_types);
    rows.count = stmt.values.size();

    // Resize each column vector to hold the rows.
    for (size_t c = 0; c < col_types.size(); ++c) {
        auto& vec = rows.columns[c];
        TypeId t = col_types[c];
        if (t == TypeId::VARCHAR) {
            vec.str_data.resize(stmt.values.size());
        } else if (t == TypeId::FLOAT32 || t == TypeId::FLOAT64) {
            vec.float_data.resize(stmt.values.size());
        } else {
            vec.int_data.resize(stmt.values.size());
        }
        vec.count = stmt.values.size();
    }

    for (size_t row_idx = 0; row_idx < stmt.values.size(); ++row_idx) {
        const auto& row = stmt.values[row_idx];
        if (row.size() != column_ids.size()) {
            throw BindError("row " + std::to_string(row_idx) + ": expected "
                            + std::to_string(column_ids.size())
                            + " values, got " + std::to_string(row.size()));
        }
        for (size_t i = 0; i < row.size(); ++i) {
            auto bound = BindExpr(*row[i], empty_ctx);
            TypeId expected = col_types[i];
            auto& vec = rows.columns[i];

            if (bound->kind == LogicalExpr::Kind::LITERAL) {
                const auto& lit = static_cast<const LogicalLit&>(*bound);
                if (lit.is_null) {
                    vec.SetNull(row_idx);
                    continue;
                }
                vec.validity.set(row_idx);
                // Store value with numeric coercion as needed.
                switch (expected) {
                    case TypeId::BOOLEAN:
                    case TypeId::INT8:
                    case TypeId::INT16:
                    case TypeId::INT32:
                    case TypeId::INT64:
                        if (lit.value.type == TypeId::INT64 ||
                            lit.value.type == TypeId::BOOLEAN) {
                            vec.int_data[row_idx] = lit.value.GetInt64();
                        } else if (lit.value.type == TypeId::FLOAT64 ||
                                   lit.value.type == TypeId::FLOAT32) {
                            vec.int_data[row_idx] = static_cast<int64_t>(lit.value.GetFloat64());
                        } else {
                            throw BindError("column '" + table->columns[column_ids[i]].name
                                            + "': cannot assign "
                                            + TypeName(lit.value.type)
                                            + " to " + TypeName(expected));
                        }
                        break;
                    case TypeId::FLOAT32:
                    case TypeId::FLOAT64:
                        if (lit.value.type == TypeId::FLOAT64 ||
                            lit.value.type == TypeId::FLOAT32) {
                            vec.float_data[row_idx] = lit.value.GetFloat64();
                        } else if (IsNumericType(lit.value.type)) {
                            vec.float_data[row_idx] = static_cast<double>(lit.value.GetInt64());
                        } else {
                            throw BindError("column '" + table->columns[column_ids[i]].name
                                            + "': cannot assign "
                                            + TypeName(lit.value.type)
                                            + " to " + TypeName(expected));
                        }
                        break;
                    case TypeId::VARCHAR:
                        if (lit.value.type == TypeId::VARCHAR) {
                            vec.str_data[row_idx] = lit.value.GetVarchar();
                        } else {
                            throw BindError("column '" + table->columns[column_ids[i]].name
                                            + "': cannot assign "
                                            + TypeName(lit.value.type)
                                            + " to VARCHAR");
                        }
                        break;
                    default:
                        throw BindError("unsupported column type in INSERT");
                }
            } else {
                throw BindError("INSERT VALUES must contain only literals");
            }
        }
    }

    auto insert = std::make_unique<LogicalInsert>();
    insert->schema_name = schema_name;
    insert->table_name  = stmt.table_name;
    insert->column_ids  = std::move(column_ids);
    insert->rows        = std::move(rows);
    return insert;
}

// ---------------------------------------------------------------------------
// UPDATE
// ---------------------------------------------------------------------------

std::unique_ptr<LogicalPlan> Binder::BindUpdate(const UpdateStatement& stmt) {
    const std::string schema_name = stmt.schema_name.empty()
                                  ? Catalog::DEFAULT_SCHEMA
                                  : stmt.schema_name;

    TableCatalogEntry* table = catalog_.GetTable(schema_name, stmt.table_name, tx_);
    if (!table) {
        throw BindError("table not found: " + stmt.table_name);
    }

    BindContext ctx;
    ctx.AddTable(0, stmt.table_name, table->ColumnNames(), table->ColumnTypes());

    // Build scan + optional filter
    auto get = std::make_unique<LogicalGet>();
    get->schema_name  = schema_name;
    get->table_name   = stmt.table_name;
    for (size_t i = 0; i < table->ColumnCount(); ++i) {
        get->column_ids.push_back(i);
    }
    get->output_types = table->ColumnTypes();
    get->output_names = table->ColumnNames();
    for (const auto& rg : table->row_groups) {
        get->catalog_row_count += rg->RowCount();
    }

    std::unique_ptr<LogicalPlan> source_owned = std::move(get);
    if (stmt.where_clause) {
        auto pred = BindExpr(*stmt.where_clause, ctx);
        if (pred->result_type != TypeId::BOOLEAN) {
            throw BindError("WHERE clause must be boolean");
        }
        auto filter = std::make_unique<LogicalFilter>();
        filter->predicate    = std::move(pred);
        filter->output_types = table->ColumnTypes();
        filter->output_names = table->ColumnNames();
        filter->children.push_back(std::move(source_owned));
        source_owned = std::move(filter);
    }

    // Resolve SET clauses
    std::vector<size_t>                       update_col_ids;
    std::vector<std::unique_ptr<LogicalExpr>> update_exprs;

    for (const auto& clause : stmt.set_clauses) {
        int idx = table->FindColumn(clause.column_name);
        if (idx < 0) {
            throw BindError("column not found: " + clause.column_name);
        }
        update_col_ids.push_back(static_cast<size_t>(idx));
        update_exprs.push_back(BindExpr(*clause.value_expr, ctx));
    }

    auto upd = std::make_unique<LogicalUpdate>();
    upd->schema_name    = schema_name;
    upd->table_name     = stmt.table_name;
    upd->update_col_ids = std::move(update_col_ids);
    upd->update_exprs   = std::move(update_exprs);
    upd->output_types   = table->ColumnTypes();
    upd->output_names   = table->ColumnNames();
    upd->children.push_back(std::move(source_owned));
    return upd;
}

// ---------------------------------------------------------------------------
// DELETE
// ---------------------------------------------------------------------------

std::unique_ptr<LogicalPlan> Binder::BindDelete(const DeleteStatement& stmt) {
    const std::string schema_name = stmt.schema_name.empty()
                                  ? Catalog::DEFAULT_SCHEMA
                                  : stmt.schema_name;

    TableCatalogEntry* table = catalog_.GetTable(schema_name, stmt.table_name, tx_);
    if (!table) {
        throw BindError("table not found: " + stmt.table_name);
    }

    BindContext ctx;
    ctx.AddTable(0, stmt.table_name, table->ColumnNames(), table->ColumnTypes());

    auto get = std::make_unique<LogicalGet>();
    get->schema_name  = schema_name;
    get->table_name   = stmt.table_name;
    for (size_t i = 0; i < table->ColumnCount(); ++i) {
        get->column_ids.push_back(i);
    }
    get->output_types = table->ColumnTypes();
    get->output_names = table->ColumnNames();
    for (const auto& rg : table->row_groups) {
        get->catalog_row_count += rg->RowCount();
    }

    std::unique_ptr<LogicalPlan> source_owned = std::move(get);
    if (stmt.where_clause) {
        auto pred = BindExpr(*stmt.where_clause, ctx);
        if (pred->result_type != TypeId::BOOLEAN) {
            throw BindError("WHERE clause must be boolean");
        }
        auto filter = std::make_unique<LogicalFilter>();
        filter->predicate    = std::move(pred);
        filter->output_types = table->ColumnTypes();
        filter->output_names = table->ColumnNames();
        filter->children.push_back(std::move(source_owned));
        source_owned = std::move(filter);
    }

    auto del = std::make_unique<LogicalDelete>();
    del->schema_name = schema_name;
    del->table_name  = stmt.table_name;
    del->output_types = table->ColumnTypes();
    del->output_names = table->ColumnNames();
    del->children.push_back(std::move(source_owned));
    return del;
}

// ---------------------------------------------------------------------------
// CREATE TABLE
// ---------------------------------------------------------------------------

std::unique_ptr<LogicalPlan> Binder::BindCreateTable(const CreateTableStatement& stmt) {
    const std::string schema_name = stmt.schema_name.empty()
                                  ? Catalog::DEFAULT_SCHEMA
                                  : stmt.schema_name;

    if (stmt.columns.empty()) {
        throw BindError("CREATE TABLE requires at least one column");
    }

    // Check for duplicate column names
    std::unordered_map<std::string, int> seen;
    for (const auto& col : stmt.columns) {
        if (++seen[col.name] > 1) {
            throw BindError("duplicate column name: " + col.name);
        }
        if (col.type == TypeId::INVALID) {
            throw BindError("invalid type for column: " + col.name);
        }
    }

    // Convert ColumnDef → ColumnDefinition
    std::vector<ColumnDefinition> col_defs;
    col_defs.reserve(stmt.columns.size());
    for (const auto& c : stmt.columns) {
        ColumnDefinition cd;
        cd.name        = c.name;
        cd.type        = c.type;
        cd.not_null    = c.not_null;
        cd.primary_key = c.primary_key;
        col_defs.push_back(std::move(cd));
    }

    auto node = std::make_unique<LogicalCreateTable>();
    node->schema_name   = schema_name;
    node->table_name    = stmt.table_name;
    node->columns       = std::move(col_defs);
    node->if_not_exists = stmt.if_not_exists;
    return node;
}

// ---------------------------------------------------------------------------
// DROP TABLE
// ---------------------------------------------------------------------------

std::unique_ptr<LogicalPlan> Binder::BindDropTable(const DropTableStatement& stmt) {
    const std::string schema_name = stmt.schema_name.empty()
                                  ? Catalog::DEFAULT_SCHEMA
                                  : stmt.schema_name;

    auto node = std::make_unique<LogicalDropTable>();
    node->schema_name = schema_name;
    node->table_name  = stmt.table_name;
    node->if_exists   = stmt.if_exists;
    return node;
}

// ---------------------------------------------------------------------------
// Expression binding
// ---------------------------------------------------------------------------

std::unique_ptr<LogicalExpr> Binder::BindExpr(const Expr& expr,
                                               const BindContext& ctx,
                                               bool allow_aggregates) {
    switch (expr.kind) {
        case Expr::Kind::COLUMN_REF: {
            const auto& e = static_cast<const ColumnRefExpr&>(expr);
            ColumnBinding binding;
            if (e.table_name.empty()) {
                binding = ctx.ResolveColumn(e.column_name);
            } else {
                binding = ctx.ResolveQualified(e.table_name, e.column_name);
            }
            auto ref = std::make_unique<BoundColumnRef>();
            ref->column_idx  = binding.column_idx;
            ref->result_type = binding.type;
            ref->name        = binding.column_name;
            return ref;
        }

        case Expr::Kind::INTEGER_LIT: {
            const auto& e = static_cast<const IntegerLitExpr&>(expr);
            auto lit = std::make_unique<LogicalLit>();
            lit->value       = Value::Integer(e.value);
            lit->result_type = TypeId::INT64;
            return lit;
        }

        case Expr::Kind::FLOAT_LIT: {
            const auto& e = static_cast<const FloatLitExpr&>(expr);
            auto lit = std::make_unique<LogicalLit>();
            lit->value       = Value::Float(e.value);
            lit->result_type = TypeId::FLOAT64;
            return lit;
        }

        case Expr::Kind::STRING_LIT: {
            const auto& e = static_cast<const StringLitExpr&>(expr);
            auto lit = std::make_unique<LogicalLit>();
            lit->value       = Value::Varchar(e.value);
            lit->result_type = TypeId::VARCHAR;
            return lit;
        }

        case Expr::Kind::BOOL_LIT: {
            const auto& e = static_cast<const BoolLitExpr&>(expr);
            auto lit = std::make_unique<LogicalLit>();
            lit->value       = Value::Boolean(e.value);
            lit->result_type = TypeId::BOOLEAN;
            return lit;
        }

        case Expr::Kind::NULL_LIT: {
            auto lit = std::make_unique<LogicalLit>();
            lit->is_null     = true;
            lit->result_type = TypeId::INVALID;
            return lit;
        }

        case Expr::Kind::BINARY_OP: {
            const auto& e = static_cast<const BinaryOpExpr&>(expr);
            return BindBinaryOp(e, ctx, allow_aggregates);
        }

        case Expr::Kind::UNARY_OP: {
            const auto& e = static_cast<const UnaryOpExpr&>(expr);
            auto child = BindExpr(*e.operand, ctx, allow_aggregates);
            if (e.op == "NOT") {
                if (child->result_type != TypeId::BOOLEAN) {
                    throw BindError("NOT requires BOOLEAN operand");
                }
            } else if (e.op == "-") {
                if (!IsNumericType(child->result_type)) {
                    throw BindError("unary minus requires numeric operand");
                }
            }
            auto unary = std::make_unique<LogicalUnaryOp>();
            unary->op          = e.op;
            unary->result_type = child->result_type;
            unary->child       = std::move(child);
            return unary;
        }

        case Expr::Kind::FUNCTION_CALL: {
            const auto& e = static_cast<const FunctionCallExpr&>(expr);
            if (!allow_aggregates) {
                throw BindError("aggregate function '" + e.name
                                + "' not allowed here");
            }
            return BindAggregate(e, ctx);
        }

        case Expr::Kind::STAR:
            throw BindError("* not allowed here");
    }
    throw BindError("BindExpr: unknown expression kind");
}

// ---------------------------------------------------------------------------
// BinaryOp binding
// ---------------------------------------------------------------------------

std::unique_ptr<LogicalExpr> Binder::BindBinaryOp(const BinaryOpExpr& expr,
                                                    const BindContext& ctx,
                                                    bool allow_aggregates) {
    auto left  = BindExpr(*expr.left,  ctx, allow_aggregates);
    auto right = BindExpr(*expr.right, ctx, allow_aggregates);

    TypeId result_type = InferBinaryType(expr.op,
                                         left->result_type, right->result_type,
                                         left, right);

    auto binop = std::make_unique<LogicalBinaryOp>();
    binop->op          = expr.op;
    binop->result_type = result_type;
    binop->left        = std::move(left);
    binop->right       = std::move(right);
    return binop;
}

// ---------------------------------------------------------------------------
// Aggregate binding
// ---------------------------------------------------------------------------

std::unique_ptr<LogicalExpr> Binder::BindAggregate(const FunctionCallExpr& expr,
                                                     const BindContext& ctx) {
    // Map function name to AggFunc
    LogicalAggrExpr::AggFunc func;
    std::string upper_name;
    upper_name.reserve(expr.name.size());
    for (char c : expr.name) upper_name += static_cast<char>(std::toupper(c));

    if      (upper_name == "COUNT") func = LogicalAggrExpr::AggFunc::COUNT;
    else if (upper_name == "SUM")   func = LogicalAggrExpr::AggFunc::SUM;
    else if (upper_name == "MIN")   func = LogicalAggrExpr::AggFunc::MIN;
    else if (upper_name == "MAX")   func = LogicalAggrExpr::AggFunc::MAX;
    else if (upper_name == "AVG")   func = LogicalAggrExpr::AggFunc::AVG;
    else throw BindError("unknown aggregate function: " + expr.name);

    auto aggr = std::make_unique<LogicalAggrExpr>();
    aggr->func    = func;
    aggr->is_star = expr.is_star;

    if (expr.is_star) {
        // COUNT(*) — no argument
        aggr->result_type = TypeId::INT64;
    } else {
        if (expr.args.empty()) {
            throw BindError("aggregate '" + expr.name + "' requires an argument");
        }
        aggr->arg = BindExpr(*expr.args[0], ctx, /*allow_aggregates=*/false);

        switch (func) {
            case LogicalAggrExpr::AggFunc::COUNT:
                aggr->result_type = TypeId::INT64;
                break;
            case LogicalAggrExpr::AggFunc::SUM:
            case LogicalAggrExpr::AggFunc::MIN:
            case LogicalAggrExpr::AggFunc::MAX:
                aggr->result_type = aggr->arg->result_type;
                break;
            case LogicalAggrExpr::AggFunc::AVG:
                // AVG always returns FLOAT64
                aggr->result_type = TypeId::FLOAT64;
                if (IsIntegerType(aggr->arg->result_type)) {
                    aggr->arg = Coerce(std::move(aggr->arg), TypeId::FLOAT64);
                }
                break;
        }
    }

    return aggr;
}

// ---------------------------------------------------------------------------
// Coerce
// ---------------------------------------------------------------------------

std::unique_ptr<LogicalExpr> Binder::Coerce(std::unique_ptr<LogicalExpr> expr,
                                              TypeId to_type) {
    if (expr->result_type == to_type) {
        return expr;
    }
    auto cast = std::make_unique<LogicalCast>();
    cast->from_type   = expr->result_type;
    cast->result_type = to_type;
    cast->child       = std::move(expr);
    return cast;
}

// ---------------------------------------------------------------------------
// ExpandStar
// ---------------------------------------------------------------------------

std::vector<std::unique_ptr<LogicalExpr>> Binder::ExpandStar(const BindContext& ctx) {
    std::vector<std::unique_ptr<LogicalExpr>> result;

    // Iterate tables in insertion order (table_order_) for deterministic output.
    for (const auto& alias : ctx.TableOrder()) {
        for (const auto& b : ctx.Tables().at(alias)) {
            auto ref = std::make_unique<BoundColumnRef>();
            ref->column_idx  = b.column_idx;
            ref->result_type = b.type;
            ref->name        = b.column_name;
            result.push_back(std::move(ref));
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// InferBinaryType
// ---------------------------------------------------------------------------

TypeId Binder::InferBinaryType(const std::string& op,
                                TypeId left_type, TypeId right_type,
                                std::unique_ptr<LogicalExpr>& left,
                                std::unique_ptr<LogicalExpr>& right) {
    if (op == "+" || op == "-" || op == "*" || op == "/") {
        if (!IsNumericType(left_type)) {
            throw BindError("operator '" + op + "' requires numeric left operand");
        }
        if (!IsNumericType(right_type)) {
            throw BindError("operator '" + op + "' requires numeric right operand");
        }
        if (IsIntegerType(left_type) && IsIntegerType(right_type)) {
            return TypeId::INT64;
        }
        // Mixed or float — promote both to FLOAT64
        if (left_type != TypeId::FLOAT64) {
            left = Coerce(std::move(left), TypeId::FLOAT64);
        }
        if (right_type != TypeId::FLOAT64) {
            right = Coerce(std::move(right), TypeId::FLOAT64);
        }
        return TypeId::FLOAT64;
    }

    if (IsComparisonOp(op)) {
        if (left_type == right_type) {
            return TypeId::BOOLEAN;
        }
        if (IsNumericType(left_type) && IsNumericType(right_type)) {
            // Promote to FLOAT64
            if (left_type != TypeId::FLOAT64) {
                left = Coerce(std::move(left), TypeId::FLOAT64);
            }
            if (right_type != TypeId::FLOAT64) {
                right = Coerce(std::move(right), TypeId::FLOAT64);
            }
            return TypeId::BOOLEAN;
        }
        throw BindError("cannot compare " + TypeName(left_type)
                        + " with " + TypeName(right_type));
    }

    if (op == "AND" || op == "OR") {
        if (left_type != TypeId::BOOLEAN) {
            throw BindError("AND/OR left operand must be BOOLEAN");
        }
        if (right_type != TypeId::BOOLEAN) {
            throw BindError("AND/OR right operand must be BOOLEAN");
        }
        return TypeId::BOOLEAN;
    }

    throw BindError("unknown binary operator: " + op);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool Binder::IsNumericType(TypeId t) const {
    return t == TypeId::INT8   || t == TypeId::INT16  ||
           t == TypeId::INT32  || t == TypeId::INT64  ||
           t == TypeId::FLOAT32 || t == TypeId::FLOAT64;
}

bool Binder::IsIntegerType(TypeId t) const {
    return t == TypeId::INT8 || t == TypeId::INT16 ||
           t == TypeId::INT32 || t == TypeId::INT64;
}

bool Binder::IsComparisonOp(const std::string& op) const {
    return op == "=" || op == "<>" || op == "<" ||
           op == ">" || op == "<=" || op == ">=";
}

// ---------------------------------------------------------------------------
// HasAggregateExpr
// ---------------------------------------------------------------------------

bool Binder::HasAggregateExpr(const Expr& e) const {
    if (e.kind == Expr::Kind::FUNCTION_CALL) return true;
    if (e.kind == Expr::Kind::BINARY_OP) {
        const auto& b = static_cast<const BinaryOpExpr&>(e);
        return HasAggregateExpr(*b.left) || HasAggregateExpr(*b.right);
    }
    if (e.kind == Expr::Kind::UNARY_OP) {
        const auto& u = static_cast<const UnaryOpExpr&>(e);
        return HasAggregateExpr(*u.operand);
    }
    return false;
}

// ---------------------------------------------------------------------------
// BindAggregateSelect
// Returns LogicalProjection → LogicalAggregate → scan/filter subtree.
// ---------------------------------------------------------------------------

std::unique_ptr<LogicalPlan> Binder::BindAggregateSelect(
    const SelectStatement& stmt,
    const BindContext& pre_agg_ctx,
    std::unique_ptr<LogicalPlan> scan_filter) {

    // A. Bind GROUP BY expressions (reference pre-aggregate input columns).
    std::vector<std::unique_ptr<LogicalExpr>> group_exprs;
    std::vector<std::string>  group_names;
    std::vector<TypeId>       group_types;

    for (const auto& g : stmt.group_by) {
        auto bound = BindExpr(*g, pre_agg_ctx);
        std::string name = (g->kind == Expr::Kind::COLUMN_REF)
            ? static_cast<const ColumnRefExpr&>(*g).column_name
            : "?group";
        group_types.push_back(bound->result_type);
        group_names.push_back(name);
        group_exprs.push_back(std::move(bound));
    }

    // B. Collect aggregate expressions from the SELECT list (in order of appearance).
    //    Also determine the name for each aggregate output column.
    std::vector<std::unique_ptr<LogicalExpr>> aggr_exprs;
    std::vector<std::string>  aggr_names;
    std::vector<TypeId>       aggr_types;

    // Walk SELECT list and collect aggregate calls.
    std::function<void(const Expr&)> CollectAggr = [&](const Expr& e) {
        if (e.kind == Expr::Kind::FUNCTION_CALL) {
            auto bound = BindAggregate(static_cast<const FunctionCallExpr&>(e), pre_agg_ctx);
            const auto& fc = static_cast<const FunctionCallExpr&>(e);
            std::string nm = fc.is_star ? (fc.name + "(*)") : fc.name;
            aggr_types.push_back(bound->result_type);
            aggr_names.push_back(nm);
            aggr_exprs.push_back(std::move(bound));
        } else if (e.kind == Expr::Kind::BINARY_OP) {
            const auto& b = static_cast<const BinaryOpExpr&>(e);
            CollectAggr(*b.left); CollectAggr(*b.right);
        } else if (e.kind == Expr::Kind::UNARY_OP) {
            CollectAggr(*static_cast<const UnaryOpExpr&>(e).operand);
        }
    };
    for (const auto& se : stmt.select_list) {
        if (se) CollectAggr(*se);
    }

    // C. Build LogicalAggregate.
    //    Output schema: [group_cols..., agg_results...]
    std::vector<std::string> agg_out_names = group_names;
    std::vector<TypeId>      agg_out_types = group_types;
    for (auto& n : aggr_names) agg_out_names.push_back(n);
    for (auto& t : aggr_types) agg_out_types.push_back(t);

    auto agg_node = std::make_unique<LogicalAggregate>();
    for (const auto& ge : group_exprs)
        agg_node->group_exprs.push_back(CloneExpr(*ge));
    for (const auto& ae : aggr_exprs)
        agg_node->aggr_exprs.push_back(CloneExpr(*ae));
    agg_node->output_names = agg_out_names;
    agg_node->output_types = agg_out_types;
    agg_node->children.push_back(std::move(scan_filter));

    // D. Build SELECT list projection referencing aggregate output positions.
    //    group cols are at 0..G-1, agg results at G..G+A-1.
    size_t G = group_exprs.size();
    size_t agg_cursor = 0;  // index into aggr_exprs for next aggregate in SELECT list

    std::vector<std::unique_ptr<LogicalExpr>> proj_exprs;
    std::vector<std::string>  proj_names;
    std::vector<TypeId>       proj_types;

    std::function<void(const Expr&, const std::string&)> BindPostAgg;
    BindPostAgg = [&](const Expr& e, const std::string& hint_name) {
        if (e.kind == Expr::Kind::STAR) {
            for (size_t i = 0; i < agg_out_names.size(); ++i) {
                auto ref = std::make_unique<BoundColumnRef>();
                ref->column_idx  = i;
                ref->result_type = agg_out_types[i];
                ref->name        = agg_out_names[i];
                proj_names.push_back(agg_out_names[i]);
                proj_types.push_back(agg_out_types[i]);
                proj_exprs.push_back(std::move(ref));
            }
            return;
        }
        if (e.kind == Expr::Kind::FUNCTION_CALL) {
            // Map to position G + agg_cursor.
            size_t pos = G + agg_cursor++;
            auto ref = std::make_unique<BoundColumnRef>();
            ref->column_idx  = pos;
            ref->result_type = agg_out_types[pos];
            ref->name        = agg_out_names[pos];
            std::string nm = hint_name.empty() ? agg_out_names[pos] : hint_name;
            proj_names.push_back(nm);
            proj_types.push_back(agg_out_types[pos]);
            proj_exprs.push_back(std::move(ref));
            return;
        }
        if (e.kind == Expr::Kind::COLUMN_REF) {
            const auto& cr = static_cast<const ColumnRefExpr&>(e);
            // Find matching group expression by column name.
            for (size_t i = 0; i < G; ++i) {
                if (group_names[i] == cr.column_name) {
                    auto ref = std::make_unique<BoundColumnRef>();
                    ref->column_idx  = i;
                    ref->result_type = group_types[i];
                    ref->name        = group_names[i];
                    std::string nm = hint_name.empty() ? cr.column_name : hint_name;
                    proj_names.push_back(nm);
                    proj_types.push_back(group_types[i]);
                    proj_exprs.push_back(std::move(ref));
                    return;
                }
            }
            throw BindError("column '" + cr.column_name + "' must appear in GROUP BY");
        }
        throw BindError("complex expressions in SELECT with GROUP BY not supported");
    };

    for (const auto& se : stmt.select_list) {
        std::string nm;
        if (se->kind == Expr::Kind::COLUMN_REF)
            nm = static_cast<const ColumnRefExpr&>(*se).column_name;
        BindPostAgg(*se, nm);
    }

    auto proj = std::make_unique<LogicalProjection>();
    proj->exprs        = std::move(proj_exprs);
    proj->output_types = proj_types;
    proj->output_names = proj_names;
    proj->children.push_back(std::move(agg_node));
    return proj;
}

// ---------------------------------------------------------------------------
// BindSelectWithJoin
// Handles SELECT ... FROM t1 [INNER] JOIN t2 ON ... [WHERE ...] ...
// ---------------------------------------------------------------------------

// Helper: walk a bound expression looking for equi-join conditions (col = col)
// and split them into left-side and right-side key column indices.
static void ExtractEquiJoinKeys(const LogicalExpr& cond, size_t left_count,
                                std::vector<size_t>& left_keys,
                                std::vector<size_t>& right_keys) {
    if (cond.kind != LogicalExpr::Kind::BINARY_OP) return;
    const auto& b = static_cast<const LogicalBinaryOp&>(cond);
    if (b.op == "AND") {
        ExtractEquiJoinKeys(*b.left,  left_count, left_keys, right_keys);
        ExtractEquiJoinKeys(*b.right, left_count, left_keys, right_keys);
        return;
    }
    if (b.op == "=") {
        if (b.left->kind  != LogicalExpr::Kind::BOUND_COLUMN_REF ||
            b.right->kind != LogicalExpr::Kind::BOUND_COLUMN_REF) return;
        size_t li = static_cast<const BoundColumnRef&>(*b.left).column_idx;
        size_t ri = static_cast<const BoundColumnRef&>(*b.right).column_idx;
        if (li < left_count && ri >= left_count) {
            left_keys.push_back(li);
            right_keys.push_back(ri - left_count);
        } else if (ri < left_count && li >= left_count) {
            left_keys.push_back(ri);
            right_keys.push_back(li - left_count);
        }
    }
}

std::unique_ptr<LogicalPlan> Binder::BindSelectWithJoin(const SelectStatement& stmt) {
    // Resolve left (FROM) table.
    const std::string left_schema = stmt.from_schema.empty()
                                  ? Catalog::DEFAULT_SCHEMA : stmt.from_schema;
    TableCatalogEntry* left_table = catalog_.GetTable(left_schema, stmt.from_table, tx_);
    if (!left_table) throw BindError("table not found: " + stmt.from_table);

    // Resolve all right (JOIN) tables.
    std::vector<TableCatalogEntry*> right_tables;
    std::vector<std::string>        right_schemas;
    for (const auto& jc : stmt.joins) {
        std::string rs = jc.schema_name.empty() ? Catalog::DEFAULT_SCHEMA : jc.schema_name;
        TableCatalogEntry* rt = catalog_.GetTable(rs, jc.table_name, tx_);
        if (!rt) throw BindError("table not found: " + jc.table_name);
        right_tables.push_back(rt);
        right_schemas.push_back(rs);
    }

    // Build combined BindContext (all tables, with proper col_idx_offset).
    BindContext ctx;
    size_t col_offset = 0;
    ctx.AddTable(0, stmt.from_table,
                 left_table->ColumnNames(), left_table->ColumnTypes(), col_offset);
    col_offset += left_table->ColumnCount();
    for (size_t i = 0; i < stmt.joins.size(); ++i) {
        ctx.AddTable(i + 1, stmt.joins[i].table_name,
                     right_tables[i]->ColumnNames(), right_tables[i]->ColumnTypes(),
                     col_offset);
        col_offset += right_tables[i]->ColumnCount();
    }

    // Build left LogicalGet.
    auto left_get = std::make_unique<LogicalGet>();
    left_get->schema_name = left_schema;
    left_get->table_name  = stmt.from_table;
    for (size_t i = 0; i < left_table->ColumnCount(); ++i) left_get->column_ids.push_back(i);
    left_get->output_types = left_table->ColumnTypes();
    left_get->output_names = left_table->ColumnNames();
    for (const auto& rg : left_table->row_groups) left_get->catalog_row_count += rg->RowCount();

    std::unique_ptr<LogicalPlan> current_plan = std::move(left_get);

    // For each JOIN clause, build right LogicalGet and wrap in LogicalJoin.
    size_t left_count = left_table->ColumnCount(); // accumulated left-side column count
    for (size_t i = 0; i < stmt.joins.size(); ++i) {
        const auto& jc = stmt.joins[i];
        TableCatalogEntry* rtable = right_tables[i];

        // Build right LogicalGet.
        auto right_get = std::make_unique<LogicalGet>();
        right_get->schema_name = right_schemas[i];
        right_get->table_name  = jc.table_name;
        for (size_t k = 0; k < rtable->ColumnCount(); ++k) right_get->column_ids.push_back(k);
        right_get->output_types = rtable->ColumnTypes();
        right_get->output_names = rtable->ColumnNames();
        for (const auto& rg : rtable->row_groups) right_get->catalog_row_count += rg->RowCount();

        // Bind ON condition against the combined context.
        auto condition = BindExpr(*jc.condition, ctx);

        // Extract equi-join key positions.
        std::vector<size_t> left_keys, right_keys;
        ExtractEquiJoinKeys(*condition, left_count, left_keys, right_keys);

        // Join output types = current_plan types + right types.
        std::vector<TypeId>      join_types = current_plan->output_types;
        std::vector<std::string> join_names = current_plan->output_names;
        for (auto& t : rtable->ColumnTypes()) join_types.push_back(t);
        for (auto& n : rtable->ColumnNames()) join_names.push_back(n);

        auto join_node = std::make_unique<LogicalJoin>();
        join_node->condition         = std::move(condition);
        join_node->left_key_col_ids  = std::move(left_keys);
        join_node->right_key_col_ids = std::move(right_keys);
        join_node->output_types      = std::move(join_types);
        join_node->output_names      = std::move(join_names);
        join_node->children.push_back(std::move(current_plan)); // left
        join_node->children.push_back(std::move(right_get));    // right

        left_count += rtable->ColumnCount();
        current_plan = std::move(join_node);
    }

    // Apply WHERE filter (bound against combined context).
    if (stmt.where_clause) {
        auto pred = BindExpr(*stmt.where_clause, ctx);
        if (pred->result_type != TypeId::BOOLEAN)
            throw BindError("WHERE clause must be boolean");
        auto filter = std::make_unique<LogicalFilter>();
        filter->predicate    = std::move(pred);
        filter->output_types = current_plan->output_types;
        filter->output_names = current_plan->output_names;
        filter->children.push_back(std::move(current_plan));
        current_plan = std::move(filter);
    }

    // Detect aggregation.
    bool has_agg = !stmt.group_by.empty();
    if (!has_agg) {
        for (const auto& e : stmt.select_list) {
            if (e && HasAggregateExpr(*e)) { has_agg = true; break; }
        }
    }

    if (has_agg) {
        current_plan = BindAggregateSelect(stmt, ctx, std::move(current_plan));
    } else {
        // Bind SELECT list (no aggregation).
        std::vector<std::unique_ptr<LogicalExpr>> proj_exprs;
        std::vector<std::string>                  proj_names;
        std::vector<TypeId>                       proj_types;

        for (const auto& expr_ptr : stmt.select_list) {
            if (expr_ptr->kind == Expr::Kind::STAR) {
                auto expanded = ExpandStar(ctx);
                for (auto& e : expanded) {
                    proj_names.push_back(static_cast<BoundColumnRef&>(*e).name);
                    proj_types.push_back(e->result_type);
                    proj_exprs.push_back(std::move(e));
                }
            } else {
                auto bound = BindExpr(*expr_ptr, ctx, /*allow_aggregates=*/true);
                std::string name;
                if (expr_ptr->kind == Expr::Kind::COLUMN_REF)
                    name = static_cast<const ColumnRefExpr&>(*expr_ptr).column_name;
                else
                    name = "?col";
                proj_names.push_back(name);
                proj_types.push_back(bound->result_type);
                proj_exprs.push_back(std::move(bound));
            }
        }

        // ORDER BY before projection (same rationale as non-JOIN path: sort key
        // column indices reference the full join output, not the projected subset).
        if (!stmt.order_by.empty()) {
            auto sort = std::make_unique<LogicalSort>();
            for (size_t i = 0; i < stmt.order_by.size(); ++i) {
                sort->sort_keys.push_back(BindExpr(*stmt.order_by[i], ctx));
                sort->ascending.push_back(stmt.order_asc[i]);
            }
            sort->output_types = current_plan->output_types;
            sort->output_names = current_plan->output_names;
            sort->children.push_back(std::move(current_plan));
            current_plan = std::move(sort);
        }

        auto proj = std::make_unique<LogicalProjection>();
        proj->exprs        = std::move(proj_exprs);
        proj->output_types = proj_types;
        proj->output_names = proj_names;
        proj->children.push_back(std::move(current_plan));
        current_plan = std::move(proj);
    }

    if (has_agg && !stmt.order_by.empty()) {
        // Aggregate ORDER BY is bound against post-aggregate output columns.
        BindContext out_ctx;
        out_ctx.AddTable(0, "__result__", current_plan->output_names, current_plan->output_types);

        auto sort = std::make_unique<LogicalSort>();
        for (size_t i = 0; i < stmt.order_by.size(); ++i) {
            try {
                sort->sort_keys.push_back(BindExpr(*stmt.order_by[i], out_ctx));
            } catch (const BindError&) {
                if (stmt.order_by[i]->kind != Expr::Kind::COLUMN_REF) throw;
                const auto& cref = static_cast<const ColumnRefExpr&>(*stmt.order_by[i]);
                if (cref.table_name.empty()) throw;
                ColumnRefExpr unqualified;
                unqualified.column_name = cref.column_name;
                sort->sort_keys.push_back(BindExpr(unqualified, out_ctx));
            }
            sort->ascending.push_back(stmt.order_asc[i]);
        }
        sort->output_types = current_plan->output_types;
        sort->output_names = current_plan->output_names;
        sort->children.push_back(std::move(current_plan));
        current_plan = std::move(sort);
    }

    // LIMIT / OFFSET.
    if (stmt.limit.has_value()) {
        if (*stmt.limit < 0) throw BindError("LIMIT must be non-negative");
        auto lim = std::make_unique<LogicalLimit>();
        lim->limit        = *stmt.limit;
        lim->offset       = stmt.offset.value_or(0);
        lim->output_types = current_plan->output_types;
        lim->output_names = current_plan->output_names;
        lim->children.push_back(std::move(current_plan));
        current_plan = std::move(lim);
    }

    return current_plan;
}

} // namespace cppcoldb
