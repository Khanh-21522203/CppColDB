#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "cppcoldb/common/types/type_id.hpp"
#include "cppcoldb/sql/parser/ast/parsed_statement.hpp"
#include "cppcoldb/sql/planner/bind_context.hpp"
#include "cppcoldb/sql/planner/logical_plan/logical_plan.hpp"

// Owned by other agents (engine/catalog, engine/transaction). Forward-declared
// here to avoid a hard include-order dependency on modules scaffolded in
// parallel; swap for their real abstraction headers once available:
//   cppcoldb/engine/abstractions/catalog/i_catalog.hpp
//   cppcoldb/engine/abstractions/transaction/i_transaction.hpp
namespace cppcoldb::engine::catalog {
class ICatalog;
} // namespace cppcoldb::engine::catalog

namespace cppcoldb::engine::transaction {
class ITransaction;
} // namespace cppcoldb::engine::transaction

namespace cppcoldb::sql::planner {

// Binds a parsed AST statement against the catalog into a LogicalPlan,
// resolving column references and inferring/coercing expression types.
class Binder {
public:
    Binder(engine::catalog::ICatalog& catalog, engine::transaction::ITransaction& tx);

    // Dispatch to the correct Bind* method based on stmt type.
    std::unique_ptr<LogicalPlan> Bind(const parser::ParsedStatement& stmt);

private:
    // Statement binders
    std::unique_ptr<LogicalPlan> BindSelect        (const parser::SelectStatement&      stmt);
    std::unique_ptr<LogicalPlan> BindSelectWithJoin(const parser::SelectStatement&      stmt);
    std::unique_ptr<LogicalPlan> BindInsert       (const parser::InsertStatement&      stmt);
    std::unique_ptr<LogicalPlan> BindUpdate       (const parser::UpdateStatement&      stmt);
    std::unique_ptr<LogicalPlan> BindDelete       (const parser::DeleteStatement&      stmt);
    std::unique_ptr<LogicalPlan> BindCreateTable  (const parser::CreateTableStatement& stmt);
    std::unique_ptr<LogicalPlan> BindDropTable    (const parser::DropTableStatement&   stmt);
    std::unique_ptr<LogicalPlan> BindAlterTable   (const parser::AlterTableStatement&  stmt);

    // Expression binder
    std::unique_ptr<LogicalExpr> BindExpr(const parser::Expr& expr, const BindContext& ctx,
                                          bool allow_aggregates = false);

    // Helpers
    std::unique_ptr<LogicalExpr> BindBinaryOp (const parser::BinaryOpExpr&     expr,
                                                const BindContext& ctx,
                                                bool allow_aggregates);
    std::unique_ptr<LogicalExpr> BindAggregate(const parser::FunctionCallExpr& expr,
                                                const BindContext& ctx);
    std::unique_ptr<LogicalExpr> Coerce       (std::unique_ptr<LogicalExpr> expr,
                                                common::TypeId to_type);
    std::vector<std::unique_ptr<LogicalExpr>> ExpandStar(const BindContext& ctx);

    // Infer result type for a binary operation; inserts casts into left/right as needed.
    common::TypeId InferBinaryType(const std::string& op, common::TypeId left_type,
                                    common::TypeId right_type,
                                    std::unique_ptr<LogicalExpr>& left,
                                    std::unique_ptr<LogicalExpr>& right);

    bool IsNumericType(common::TypeId t) const;
    bool IsIntegerType(common::TypeId t) const;
    bool IsComparisonOp(const std::string& op) const;

    // Aggregation helpers
    bool HasAggregateExpr(const parser::Expr& e) const;
    std::unique_ptr<LogicalPlan> BindAggregateSelect(
        const parser::SelectStatement& stmt,
        const BindContext& pre_agg_ctx,
        std::unique_ptr<LogicalPlan> scan_filter);

    engine::catalog::ICatalog&         catalog_;
    engine::transaction::ITransaction& tx_;
};

} // namespace cppcoldb::sql::planner
