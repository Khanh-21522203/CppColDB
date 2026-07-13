#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "cppcoldb/sql/planner/logical_plan/logical_plan.hpp"

// Owned by another agent (engine/execution). Forward-declared here to avoid a
// hard include-order dependency on a module scaffolded in parallel; swap for
// the real interface header once available:
//   cppcoldb/engine/abstractions/execution/i_physical_operator.hpp
namespace cppcoldb::engine::execution {
class IPhysicalOperator;
} // namespace cppcoldb::engine::execution

namespace cppcoldb::sql::planner {

// Converts a bound + optimized LogicalPlan into an engine physical-operator tree.
class PhysicalPlanner {
public:
    PhysicalPlanner() = default;

    // Convert a logical plan into a physical operator tree.
    std::unique_ptr<engine::execution::IPhysicalOperator> Plan(const LogicalPlan& plan);

private:
    std::unique_ptr<engine::execution::IPhysicalOperator> PlanNode      (const LogicalPlan&        plan);
    std::unique_ptr<engine::execution::IPhysicalOperator> PlanGet       (const LogicalGet&         node);
    std::unique_ptr<engine::execution::IPhysicalOperator> PlanFilter    (const LogicalFilter&      node);
    std::unique_ptr<engine::execution::IPhysicalOperator> PlanProjection(const LogicalProjection&  node);
    std::unique_ptr<engine::execution::IPhysicalOperator> PlanLimit     (const LogicalLimit&       node);
    std::unique_ptr<engine::execution::IPhysicalOperator> PlanSort      (const LogicalSort&        node);
    std::unique_ptr<engine::execution::IPhysicalOperator> PlanAggregate (const LogicalAggregate&   node);
    std::unique_ptr<engine::execution::IPhysicalOperator> PlanJoin      (const LogicalJoin&        node);
    std::unique_ptr<engine::execution::IPhysicalOperator> PlanInsert    (const LogicalInsert&      node);
    std::unique_ptr<engine::execution::IPhysicalOperator> PlanDelete    (const LogicalDelete&      node);
    std::unique_ptr<engine::execution::IPhysicalOperator> PlanUpdate    (const LogicalUpdate&      node);
    std::unique_ptr<engine::execution::IPhysicalOperator> PlanCreateTable(const LogicalCreateTable& node);
    std::unique_ptr<engine::execution::IPhysicalOperator> PlanDropTable (const LogicalDropTable&   node);
    std::unique_ptr<engine::execution::IPhysicalOperator> PlanAlterTable(const LogicalAlterTable&  node);

    // Remap BoundColumnRef::column_idx from table-global index to chunk-position index.
    // column_ids[chunk_pos] = table_col_idx  (maps chunk position -> table column index)
    std::unique_ptr<LogicalExpr> RemapColumnRefs(
        std::unique_ptr<LogicalExpr> expr,
        const std::vector<std::size_t>& column_ids);

    // Try to find a LogicalGet in the subtree; returns nullptr if not found (e.g., JOIN/AGGREGATE child).
    static const LogicalGet* TryFindLogicalGet(const LogicalPlan& plan);

    // Optional top-k hints keyed by LogicalSort node address.
    std::unordered_map<const LogicalSort*, std::int64_t> top_k_hints_;
};

} // namespace cppcoldb::sql::planner
