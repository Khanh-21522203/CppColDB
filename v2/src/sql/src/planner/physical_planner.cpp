#include "cppcoldb/sql/planner/physical_planner.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::sql::planner {

std::unique_ptr<engine::execution::IPhysicalOperator> PhysicalPlanner::Plan(const LogicalPlan&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

std::unique_ptr<engine::execution::IPhysicalOperator> PhysicalPlanner::PlanNode(
    const LogicalPlan&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<engine::execution::IPhysicalOperator> PhysicalPlanner::PlanGet(const LogicalGet&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<engine::execution::IPhysicalOperator> PhysicalPlanner::PlanFilter(
    const LogicalFilter&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<engine::execution::IPhysicalOperator> PhysicalPlanner::PlanProjection(
    const LogicalProjection&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<engine::execution::IPhysicalOperator> PhysicalPlanner::PlanLimit(
    const LogicalLimit&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<engine::execution::IPhysicalOperator> PhysicalPlanner::PlanSort(
    const LogicalSort&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<engine::execution::IPhysicalOperator> PhysicalPlanner::PlanAggregate(
    const LogicalAggregate&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<engine::execution::IPhysicalOperator> PhysicalPlanner::PlanJoin(
    const LogicalJoin&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<engine::execution::IPhysicalOperator> PhysicalPlanner::PlanInsert(
    const LogicalInsert&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<engine::execution::IPhysicalOperator> PhysicalPlanner::PlanDelete(
    const LogicalDelete&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<engine::execution::IPhysicalOperator> PhysicalPlanner::PlanUpdate(
    const LogicalUpdate&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<engine::execution::IPhysicalOperator> PhysicalPlanner::PlanCreateTable(
    const LogicalCreateTable&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<engine::execution::IPhysicalOperator> PhysicalPlanner::PlanDropTable(
    const LogicalDropTable&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<engine::execution::IPhysicalOperator> PhysicalPlanner::PlanAlterTable(
    const LogicalAlterTable&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

std::unique_ptr<LogicalExpr> PhysicalPlanner::RemapColumnRefs(
    std::unique_ptr<LogicalExpr>, const std::vector<std::size_t>&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

const LogicalGet* PhysicalPlanner::TryFindLogicalGet(const LogicalPlan&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::sql::planner
