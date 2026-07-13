#include "cppcoldb/sql/planner/optimizer.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::sql::planner {

Optimizer::Optimizer(OptimizerConfig cfg) : cfg_(cfg) {}

std::unique_ptr<LogicalPlan> Optimizer::Optimize(std::unique_ptr<LogicalPlan>) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

std::unique_ptr<LogicalPlan> Optimizer::RunConstantFolding(std::unique_ptr<LogicalPlan>) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<LogicalPlan> Optimizer::RunPredicatePushdown(std::unique_ptr<LogicalPlan>) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<LogicalPlan> Optimizer::RunColumnPruning(std::unique_ptr<LogicalPlan>) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<LogicalPlan> Optimizer::RunFilterMerge(std::unique_ptr<LogicalPlan>) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

std::unique_ptr<LogicalExpr> Optimizer::FoldExpr(std::unique_ptr<LogicalExpr>) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<LogicalPlan> Optimizer::FoldPlan(std::unique_ptr<LogicalPlan>) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

std::unique_ptr<LogicalPlan> Optimizer::Pushdown(std::unique_ptr<LogicalPlan>,
                                                  std::vector<std::unique_ptr<LogicalExpr>>&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void Optimizer::CollectColumnRefs(const LogicalExpr&, std::set<std::size_t>&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<LogicalPlan> Optimizer::PruneColumns(std::unique_ptr<LogicalPlan>,
                                                      std::set<std::size_t>&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

std::unique_ptr<LogicalPlan> Optimizer::MergeFilters(std::unique_ptr<LogicalPlan>) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

bool Optimizer::IsTrivialPlan(const LogicalPlan&) { CPPCOLDB_NOT_IMPLEMENTED(); }

} // namespace cppcoldb::sql::planner
