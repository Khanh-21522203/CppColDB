#pragma once
#include <memory>
#include <unordered_map>
#include <vector>
#include "planner/logical_plan/logical_plan.hpp"
#include "execution/physical_operator.hpp"

namespace cppcoldb {

class PhysicalPlanner {
public:
    PhysicalPlanner() = default;

    // Convert a logical plan into a physical operator tree.
    std::unique_ptr<PhysicalOperator> Plan(const LogicalPlan& plan);

private:
    std::unique_ptr<PhysicalOperator> PlanNode      (const LogicalPlan&       plan);
    std::unique_ptr<PhysicalOperator> PlanGet        (const LogicalGet&        node);
    std::unique_ptr<PhysicalOperator> PlanFilter     (const LogicalFilter&     node);
    std::unique_ptr<PhysicalOperator> PlanProjection (const LogicalProjection& node);
    std::unique_ptr<PhysicalOperator> PlanLimit      (const LogicalLimit&      node);
    std::unique_ptr<PhysicalOperator> PlanSort       (const LogicalSort&       node);
    std::unique_ptr<PhysicalOperator> PlanAggregate  (const LogicalAggregate&  node);
    std::unique_ptr<PhysicalOperator> PlanJoin       (const LogicalJoin&       node);
    std::unique_ptr<PhysicalOperator> PlanInsert     (const LogicalInsert&     node);
    std::unique_ptr<PhysicalOperator> PlanDelete     (const LogicalDelete&     node);
    std::unique_ptr<PhysicalOperator> PlanUpdate     (const LogicalUpdate&     node);
    std::unique_ptr<PhysicalOperator> PlanCreateTable(const LogicalCreateTable& node);
    std::unique_ptr<PhysicalOperator> PlanDropTable  (const LogicalDropTable&  node);
    std::unique_ptr<PhysicalOperator> PlanAlterTable (const LogicalAlterTable& node);

    // Remap BoundColumnRef::column_idx from table-global index to chunk-position index.
    // column_ids[chunk_pos] = table_col_idx  (maps chunk position -> table column index)
    std::unique_ptr<LogicalExpr> RemapColumnRefs(
        std::unique_ptr<LogicalExpr> expr,
        const std::vector<size_t>& column_ids);

    // Try to find a LogicalGet in the subtree; returns nullptr if not found (e.g., JOIN/AGGREGATE child).
    static const LogicalGet* TryFindLogicalGet(const LogicalPlan& plan);

    // Optional top-k hints keyed by LogicalSort node address.
    std::unordered_map<const LogicalSort*, int64_t> top_k_hints_;
};

} // namespace cppcoldb
