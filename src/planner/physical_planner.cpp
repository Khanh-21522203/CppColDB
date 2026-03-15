#include "planner/physical_planner.hpp"
#include "execution/operator/physical_table_scan.hpp"
#include "execution/operator/physical_filter.hpp"
#include "execution/operator/physical_projection.hpp"
#include "execution/operator/physical_limit.hpp"
#include "execution/operator/physical_create_table.hpp"
#include "execution/operator/physical_drop_table.hpp"
#include "common/exception.hpp"
#include <unordered_map>

namespace cppcoldb {

// ---------------------------------------------------------------------------
// Helper: walk the plan tree to find the first LogicalGet node
// ---------------------------------------------------------------------------

static const LogicalGet& FindLogicalGet(const LogicalPlan& plan) {
    if (plan.node_type == LogicalPlan::Type::GET) {
        return static_cast<const LogicalGet&>(plan);
    }
    for (const auto& child : plan.children) {
        if (child) return FindLogicalGet(*child);
    }
    throw RuntimeError("PhysicalPlanner: no LogicalGet found in plan");
}

// ---------------------------------------------------------------------------
// Plan — entry point
// ---------------------------------------------------------------------------

std::unique_ptr<PhysicalOperator> PhysicalPlanner::Plan(const LogicalPlan& plan) {
    return PlanNode(plan);
}

// ---------------------------------------------------------------------------
// PlanNode — dispatch on node type
// ---------------------------------------------------------------------------

std::unique_ptr<PhysicalOperator> PhysicalPlanner::PlanNode(const LogicalPlan& plan) {
    switch (plan.node_type) {
        case LogicalPlan::Type::GET:
            return PlanGet(static_cast<const LogicalGet&>(plan));
        case LogicalPlan::Type::FILTER:
            return PlanFilter(static_cast<const LogicalFilter&>(plan));
        case LogicalPlan::Type::PROJECTION:
            return PlanProjection(static_cast<const LogicalProjection&>(plan));
        case LogicalPlan::Type::LIMIT:
            return PlanLimit(static_cast<const LogicalLimit&>(plan));
        case LogicalPlan::Type::SORT:
            return PlanSort(static_cast<const LogicalSort&>(plan));
        case LogicalPlan::Type::INSERT:
            return PlanInsert(static_cast<const LogicalInsert&>(plan));
        case LogicalPlan::Type::DELETE:
            return PlanDelete(static_cast<const LogicalDelete&>(plan));
        case LogicalPlan::Type::UPDATE:
            return PlanUpdate(static_cast<const LogicalUpdate&>(plan));
        case LogicalPlan::Type::CREATE_TABLE:
            return PlanCreateTable(static_cast<const LogicalCreateTable&>(plan));
        case LogicalPlan::Type::DROP_TABLE:
            return PlanDropTable(static_cast<const LogicalDropTable&>(plan));
        case LogicalPlan::Type::AGGREGATE:
            throw RuntimeError("PhysicalPlanner: aggregate not yet implemented — Phase 8");
        default:
            throw RuntimeError("PhysicalPlanner: unknown logical plan node type");
    }
}

// ---------------------------------------------------------------------------
// RemapColumnRefs
// ---------------------------------------------------------------------------

std::unique_ptr<LogicalExpr> PhysicalPlanner::RemapColumnRefs(
    std::unique_ptr<LogicalExpr> expr,
    const std::vector<size_t>& column_ids) {

    if (!expr) return expr;

    // Build map: table_col_idx -> chunk_position
    std::unordered_map<size_t, size_t> remap;
    for (size_t i = 0; i < column_ids.size(); ++i) {
        remap[column_ids[i]] = i;
    }

    switch (expr->kind) {
        case LogicalExpr::Kind::BOUND_COLUMN_REF: {
            auto& ref = static_cast<BoundColumnRef&>(*expr);
            auto it = remap.find(ref.column_idx);
            if (it != remap.end()) {
                ref.column_idx = it->second;
            }
            return expr;
        }
        case LogicalExpr::Kind::BINARY_OP: {
            auto& b = static_cast<LogicalBinaryOp&>(*expr);
            b.left  = RemapColumnRefs(std::move(b.left),  column_ids);
            b.right = RemapColumnRefs(std::move(b.right), column_ids);
            return expr;
        }
        case LogicalExpr::Kind::UNARY_OP: {
            auto& u = static_cast<LogicalUnaryOp&>(*expr);
            u.child = RemapColumnRefs(std::move(u.child), column_ids);
            return expr;
        }
        case LogicalExpr::Kind::CAST: {
            auto& c = static_cast<LogicalCast&>(*expr);
            c.child = RemapColumnRefs(std::move(c.child), column_ids);
            return expr;
        }
        case LogicalExpr::Kind::LITERAL:
        case LogicalExpr::Kind::AGGREGATE:
            return expr;
        default:
            return expr;
    }
}

// ---------------------------------------------------------------------------
// PlanGet
// ---------------------------------------------------------------------------

std::unique_ptr<PhysicalOperator> PhysicalPlanner::PlanGet(const LogicalGet& node) {
    auto op = std::make_unique<PhysicalTableScan>();
    op->schema_name  = node.schema_name;
    op->table_name   = node.table_name;
    op->column_ids   = node.column_ids;
    op->output_types = node.output_types;
    op->output_names = node.output_names;

    // Clone pushed_filters, remapping column refs to chunk positions
    for (const auto& f : node.pushed_filters) {
        if (f) {
            op->pushed_filters.push_back(
                RemapColumnRefs(CloneExpr(*f), node.column_ids));
        }
    }
    return op;
}

// ---------------------------------------------------------------------------
// PlanFilter
// ---------------------------------------------------------------------------

std::unique_ptr<PhysicalOperator> PhysicalPlanner::PlanFilter(const LogicalFilter& node) {
    const LogicalGet& get = FindLogicalGet(node);

    auto child = PlanNode(*node.children[0]);

    auto op = std::make_unique<PhysicalFilter>();
    op->predicate = node.predicate
        ? RemapColumnRefs(CloneExpr(*node.predicate), get.column_ids)
        : nullptr;
    op->output_types = node.output_types;
    op->output_names = node.output_names;
    op->children.push_back(std::move(child));
    return op;
}

// ---------------------------------------------------------------------------
// PlanProjection
// ---------------------------------------------------------------------------

std::unique_ptr<PhysicalOperator> PhysicalPlanner::PlanProjection(
    const LogicalProjection& node) {

    const LogicalGet& get = FindLogicalGet(node);

    auto child = PlanNode(*node.children[0]);

    auto op = std::make_unique<PhysicalProjection>();
    for (const auto& e : node.exprs) {
        if (e) {
            op->exprs.push_back(RemapColumnRefs(CloneExpr(*e), get.column_ids));
        }
    }
    op->output_types = node.output_types;
    op->output_names = node.output_names;
    op->children.push_back(std::move(child));
    return op;
}

// ---------------------------------------------------------------------------
// PlanLimit
// ---------------------------------------------------------------------------

std::unique_ptr<PhysicalOperator> PhysicalPlanner::PlanLimit(const LogicalLimit& node) {
    auto child = PlanNode(*node.children[0]);

    auto op = std::make_unique<PhysicalLimit>();
    op->limit        = node.limit;
    op->offset       = node.offset;
    op->output_types = node.output_types;
    op->output_names = node.output_names;
    op->children.push_back(std::move(child));
    return op;
}

// ---------------------------------------------------------------------------
// PlanSort — passthrough (ORDER BY execution deferred)
// ---------------------------------------------------------------------------

std::unique_ptr<PhysicalOperator> PhysicalPlanner::PlanSort(const LogicalSort& node) {
    return PlanNode(*node.children[0]);
}

// ---------------------------------------------------------------------------
// PlanCreateTable
// ---------------------------------------------------------------------------

std::unique_ptr<PhysicalOperator> PhysicalPlanner::PlanCreateTable(
    const LogicalCreateTable& node) {

    auto op = std::make_unique<PhysicalCreateTable>();
    op->schema_name   = node.schema_name;
    op->table_name    = node.table_name;
    op->columns       = node.columns;
    op->if_not_exists = node.if_not_exists;
    op->output_types  = {};
    op->output_names  = {};
    return op;
}

// ---------------------------------------------------------------------------
// PlanDropTable
// ---------------------------------------------------------------------------

std::unique_ptr<PhysicalOperator> PhysicalPlanner::PlanDropTable(
    const LogicalDropTable& node) {

    auto op = std::make_unique<PhysicalDropTable>();
    op->schema_name  = node.schema_name;
    op->table_name   = node.table_name;
    op->if_exists    = node.if_exists;
    op->output_types = {};
    op->output_names = {};
    return op;
}

// ---------------------------------------------------------------------------
// PlanInsert / PlanDelete / PlanUpdate — deferred to Phase 8
// ---------------------------------------------------------------------------

std::unique_ptr<PhysicalOperator> PhysicalPlanner::PlanInsert(const LogicalInsert&) {
    throw RuntimeError("PhysicalPlanner: INSERT not yet implemented — Phase 8");
}

std::unique_ptr<PhysicalOperator> PhysicalPlanner::PlanDelete(const LogicalDelete&) {
    throw RuntimeError("PhysicalPlanner: DELETE not yet implemented — Phase 8");
}

std::unique_ptr<PhysicalOperator> PhysicalPlanner::PlanUpdate(const LogicalUpdate&) {
    throw RuntimeError("PhysicalPlanner: UPDATE not yet implemented — Phase 8");
}

} // namespace cppcoldb
