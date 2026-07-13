#include "cppcoldb/sql/planner/binder.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::sql::planner {

Binder::Binder(engine::catalog::ICatalog& catalog, engine::transaction::ITransaction& tx)
    : catalog_(catalog), tx_(tx) {}

std::unique_ptr<LogicalPlan> Binder::Bind(const parser::ParsedStatement&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

std::unique_ptr<LogicalPlan> Binder::BindSelect(const parser::SelectStatement&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<LogicalPlan> Binder::BindSelectWithJoin(const parser::SelectStatement&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<LogicalPlan> Binder::BindInsert(const parser::InsertStatement&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<LogicalPlan> Binder::BindUpdate(const parser::UpdateStatement&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<LogicalPlan> Binder::BindDelete(const parser::DeleteStatement&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<LogicalPlan> Binder::BindCreateTable(const parser::CreateTableStatement&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<LogicalPlan> Binder::BindDropTable(const parser::DropTableStatement&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<LogicalPlan> Binder::BindAlterTable(const parser::AlterTableStatement&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

std::unique_ptr<LogicalExpr> Binder::BindExpr(const parser::Expr&, const BindContext&, bool) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

std::unique_ptr<LogicalExpr> Binder::BindBinaryOp(const parser::BinaryOpExpr&, const BindContext&,
                                                   bool) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<LogicalExpr> Binder::BindAggregate(const parser::FunctionCallExpr&,
                                                    const BindContext&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<LogicalExpr> Binder::Coerce(std::unique_ptr<LogicalExpr>, common::TypeId) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::vector<std::unique_ptr<LogicalExpr>> Binder::ExpandStar(const BindContext&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

common::TypeId Binder::InferBinaryType(const std::string&, common::TypeId, common::TypeId,
                                        std::unique_ptr<LogicalExpr>&,
                                        std::unique_ptr<LogicalExpr>&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

bool Binder::IsNumericType(common::TypeId) const { CPPCOLDB_NOT_IMPLEMENTED(); }
bool Binder::IsIntegerType(common::TypeId) const { CPPCOLDB_NOT_IMPLEMENTED(); }
bool Binder::IsComparisonOp(const std::string&) const { CPPCOLDB_NOT_IMPLEMENTED(); }

bool Binder::HasAggregateExpr(const parser::Expr&) const { CPPCOLDB_NOT_IMPLEMENTED(); }
std::unique_ptr<LogicalPlan> Binder::BindAggregateSelect(const parser::SelectStatement&,
                                                          const BindContext&,
                                                          std::unique_ptr<LogicalPlan>) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::sql::planner
