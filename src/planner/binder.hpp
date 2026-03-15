#pragma once
#include <memory>
#include <vector>
#include "planner/logical_plan/logical_plan.hpp"
#include "planner/bind_context.hpp"
#include "parser/ast/parsed_statement.hpp"

namespace cppcoldb {

class Catalog;
class Transaction;

class Binder {
public:
    Binder(Catalog& catalog, Transaction& tx);

    // Dispatch to the correct Bind* method based on stmt type.
    std::unique_ptr<LogicalPlan> Bind(const ParsedStatement& stmt);

private:
    // Statement binders
    std::unique_ptr<LogicalPlan> BindSelect     (const SelectStatement&      stmt);
    std::unique_ptr<LogicalPlan> BindInsert      (const InsertStatement&      stmt);
    std::unique_ptr<LogicalPlan> BindUpdate      (const UpdateStatement&      stmt);
    std::unique_ptr<LogicalPlan> BindDelete      (const DeleteStatement&      stmt);
    std::unique_ptr<LogicalPlan> BindCreateTable (const CreateTableStatement& stmt);
    std::unique_ptr<LogicalPlan> BindDropTable   (const DropTableStatement&   stmt);

    // Expression binder
    std::unique_ptr<LogicalExpr> BindExpr(const Expr& expr, const BindContext& ctx,
                                          bool allow_aggregates = false);

    // Helpers
    std::unique_ptr<LogicalExpr> BindBinaryOp (const BinaryOpExpr&     expr,
                                                const BindContext& ctx,
                                                bool allow_aggregates);
    std::unique_ptr<LogicalExpr> BindAggregate(const FunctionCallExpr& expr,
                                                const BindContext& ctx);
    std::unique_ptr<LogicalExpr> Coerce       (std::unique_ptr<LogicalExpr> expr,
                                                TypeId to_type);
    std::vector<std::unique_ptr<LogicalExpr>> ExpandStar(const BindContext& ctx);

    // Infer result type for a binary operation; inserts casts into left/right as needed.
    TypeId InferBinaryType(const std::string& op, TypeId left_type, TypeId right_type,
                           std::unique_ptr<LogicalExpr>& left,
                           std::unique_ptr<LogicalExpr>& right);

    bool IsNumericType(TypeId t) const;
    bool IsIntegerType(TypeId t) const;
    bool IsComparisonOp(const std::string& op) const;

    Catalog&     catalog_;
    Transaction& tx_;
};

} // namespace cppcoldb
