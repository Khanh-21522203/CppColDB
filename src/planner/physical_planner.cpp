#include "planner/physical_planner.hpp"
#include "execution/operator/physical_delete.hpp"
#include "execution/operator/physical_update.hpp"
#include "execution/operator/physical_sort.hpp"
#include "execution/operator/physical_sort_source.hpp"
#include "execution/operator/physical_table_scan.hpp"
#include "execution/operator/physical_filter.hpp"
#include "execution/operator/physical_projection.hpp"
#include "execution/operator/physical_limit.hpp"
#include "execution/operator/physical_create_table.hpp"
#include "execution/operator/physical_drop_table.hpp"
#include "execution/operator/physical_hash_aggregation.hpp"
#include "execution/operator/physical_aggregation_source.hpp"
#include "execution/operator/physical_hash_join.hpp"
#include "execution/operator/physical_insert.hpp"
#include "common/exception.hpp"
#include <algorithm>
#include <cstdint>
#include <unordered_map>

namespace cppcoldb {

namespace {

const LogicalSort* FindTopKSortCandidate(const LogicalPlan* node) {
    const LogicalPlan* cur = node;
    while (cur) {
        if (cur->node_type == LogicalPlan::Type::SORT) {
            return static_cast<const LogicalSort*>(cur);
        }
        // Projection preserves row order/cardinality, so LIMIT can push top-k through it.
        if (cur->node_type == LogicalPlan::Type::PROJECTION &&
            cur->children.size() == 1 &&
            cur->children[0]) {
            cur = cur->children[0].get();
            continue;
        }
        return nullptr;
    }
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// Helper: try to find a LogicalGet node; nullptr if none (JOIN/AGGREGATE child)
// ---------------------------------------------------------------------------

const LogicalGet* PhysicalPlanner::TryFindLogicalGet(const LogicalPlan& plan) {
    if (plan.node_type == LogicalPlan::Type::GET) {
        return static_cast<const LogicalGet*>(&plan);
    }
    // Don't recurse into AGGREGATE or JOIN children — they have their own output schema.
    if (plan.node_type == LogicalPlan::Type::AGGREGATE ||
        plan.node_type == LogicalPlan::Type::JOIN) {
        return nullptr;
    }
    for (const auto& child : plan.children) {
        if (child) {
            const LogicalGet* g = TryFindLogicalGet(*child);
            if (g) return g;
        }
    }
    return nullptr;
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
            return PlanAggregate(static_cast<const LogicalAggregate&>(plan));
        case LogicalPlan::Type::JOIN:
            return PlanJoin(static_cast<const LogicalJoin&>(plan));
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
    const LogicalGet* get = TryFindLogicalGet(node);

    auto child = PlanNode(*node.children[0]);

    auto op = std::make_unique<PhysicalFilter>();
    if (node.predicate) {
        if (get) {
            op->predicate = RemapColumnRefs(CloneExpr(*node.predicate), get->column_ids);
        } else {
            op->predicate = CloneExpr(*node.predicate);
        }
    }
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

    const LogicalGet* get = TryFindLogicalGet(node);

    auto child = PlanNode(*node.children[0]);

    auto op = std::make_unique<PhysicalProjection>();
    for (const auto& e : node.exprs) {
        if (e) {
            if (get) {
                op->exprs.push_back(RemapColumnRefs(CloneExpr(*e), get->column_ids));
            } else {
                op->exprs.push_back(CloneExpr(*e));
            }
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
    std::unique_ptr<PhysicalOperator> child;
    const LogicalSort* hinted_sort = nullptr;
    if (!node.children.empty() && node.children[0] && node.limit >= 0) {
        hinted_sort = FindTopKSortCandidate(node.children[0].get());
        if (hinted_sort) {
            int64_t eff_offset = std::max<int64_t>(0, node.offset);
            int64_t top_k = node.limit;
            if (top_k > INT64_MAX - eff_offset) {
                top_k = INT64_MAX;
            } else {
                top_k += eff_offset;
            }
            top_k_hints_[hinted_sort] = top_k;
        }
    }
    child = PlanNode(*node.children[0]);
    if (hinted_sort) {
        top_k_hints_.erase(hinted_sort);
    }

    auto op = std::make_unique<PhysicalLimit>();
    op->limit        = node.limit;
    op->offset       = node.offset;
    op->output_types = node.output_types;
    op->output_names = node.output_names;
    op->children.push_back(std::move(child));
    return op;
}

// ---------------------------------------------------------------------------
// PlanSort — two-pipeline SINK+SOURCE pattern (mirrors PlanAggregate)
// ---------------------------------------------------------------------------

std::unique_ptr<PhysicalOperator> PhysicalPlanner::PlanSort(const LogicalSort& node) {
    // Shared sort buffer between SINK and SOURCE.
    auto shared_buf = std::make_shared<SortBuffer>();

    // Build SINK operator.
    auto sink          = std::make_unique<PhysicalSort>();
    sink->buf          = shared_buf;
    auto hint_it = top_k_hints_.find(&node);
    sink->top_k        = (hint_it != top_k_hints_.end()) ? hint_it->second : -1;
    sink->output_types = node.output_types;
    sink->output_names = node.output_names;

    const LogicalGet* get = TryFindLogicalGet(node);
    for (size_t ki = 0; ki < node.sort_keys.size(); ++ki) {
        if (get) {
            sink->sort_keys.push_back(
                RemapColumnRefs(CloneExpr(*node.sort_keys[ki]), get->column_ids));
        } else {
            sink->sort_keys.push_back(CloneExpr(*node.sort_keys[ki]));
        }
        sink->ascending.push_back(node.ascending[ki]);
    }

    // Child provides input rows (the scan/filter/projection subtree).
    sink->children.push_back(PlanNode(*node.children[0]));

    // Build SOURCE operator.
    auto source          = std::make_unique<PhysicalSortSource>();
    source->buf          = shared_buf;
    source->output_types = node.output_types;
    source->output_names = node.output_names;

    // Attach source as a child of the sink so Executor::BuildPipelines can reach it.
    sink->source_op = std::move(source);

    return sink;
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
// PlanAggregate
// ---------------------------------------------------------------------------

std::unique_ptr<PhysicalOperator> PhysicalPlanner::PlanAggregate(
    const LogicalAggregate& node) {

    size_t num_agg = node.aggr_exprs.size();

    // Shared hash table between SINK and SOURCE.
    auto ht = std::make_shared<AggregateHashTable>(num_agg);

    // Build SINK operator.
    auto sink = std::make_unique<PhysicalHashAggregation>();
    sink->ht          = ht;
    sink->output_types = node.output_types;
    sink->output_names = node.output_names;

    for (const auto& ge : node.group_exprs) {
        // Group exprs reference pre-agg input; remap if there's a LogicalGet child.
        const LogicalGet* get = TryFindLogicalGet(node);
        if (get) {
            sink->group_exprs.push_back(RemapColumnRefs(CloneExpr(*ge), get->column_ids));
        } else {
            sink->group_exprs.push_back(CloneExpr(*ge));
        }
    }
    for (const auto& ae : node.aggr_exprs) {
        // Aggregate exprs may contain a child arg that references pre-agg columns.
        const LogicalGet* get = TryFindLogicalGet(node);
        if (get) {
            sink->aggr_exprs.push_back(RemapColumnRefs(CloneExpr(*ae), get->column_ids));
        } else {
            sink->aggr_exprs.push_back(CloneExpr(*ae));
        }
        const auto& agg = static_cast<const LogicalAggrExpr&>(*ae);
        (void)agg; // result types already in node.output_types
    }

    // Record result types for aggregate columns (after group cols).
    size_t G = node.group_exprs.size();
    for (size_t j = G; j < node.output_types.size(); ++j) {
        sink->aggr_result_types.push_back(node.output_types[j]);
    }

    // Build scan/filter child.
    sink->children.push_back(PlanNode(*node.children[0]));

    // Build SOURCE operator.
    auto source = std::make_unique<PhysicalAggregationSource>();
    source->ht             = ht;
    source->output_types   = node.output_types;
    source->output_names   = node.output_names;
    source->num_group_cols = G;
    for (size_t j = 0; j < num_agg; ++j) {
        const auto& ae = static_cast<const LogicalAggrExpr&>(*node.aggr_exprs[j]);
        source->agg_funcs.push_back(ae.func);
        source->agg_result_types.push_back(node.output_types[G + j]);
    }

    // Attach source as a child of the sink so Executor::BuildPipelines can reach it.
    sink->source_op = std::move(source);

    return sink;
}

// ---------------------------------------------------------------------------
// PlanJoin
// ---------------------------------------------------------------------------

std::unique_ptr<PhysicalOperator> PhysicalPlanner::PlanJoin(const LogicalJoin& node) {
    // Shared hash table between build (SINK) and probe (OPERATOR).
    auto ht = std::make_shared<JoinHashTable>();

    // Build side: plan the right child (children[1]).
    auto right_child = PlanNode(*node.children[1]);

    auto build = std::make_unique<PhysicalHashJoinBuild>();
    build->ht           = ht;
    build->key_col_idxs = node.right_key_col_ids;
    build->output_types = right_child->output_types;
    build->output_names = right_child->output_names;
    build->children.push_back(std::move(right_child));

    // Probe side: plan the left child (children[0]).
    auto left_child = PlanNode(*node.children[0]);

    auto probe = std::make_unique<PhysicalHashJoinProbe>();
    probe->ht                 = ht;
    probe->left_key_col_idxs  = node.left_key_col_ids;
    probe->left_col_count     = node.children[0]->output_types.size();
    probe->output_types       = node.output_types;
    probe->output_names       = node.output_names;
    probe->build_op           = std::move(build); // executor uses this for build pipeline
    probe->children.push_back(std::move(left_child));

    return probe;
}

// ---------------------------------------------------------------------------
// PlanInsert / PlanDelete / PlanUpdate — deferred
// ---------------------------------------------------------------------------

std::unique_ptr<PhysicalOperator> PhysicalPlanner::PlanInsert(const LogicalInsert& node) {
    auto op = std::make_unique<PhysicalInsert>();
    op->schema_name  = node.schema_name;
    op->table_name   = node.table_name;
    op->column_ids   = node.column_ids;
    op->rows         = node.rows;  // copy pre-evaluated DataChunk
    op->output_types = {};
    op->output_names = {};
    return op;
}

std::unique_ptr<PhysicalOperator> PhysicalPlanner::PlanDelete(const LogicalDelete& node) {
    const LogicalGet* get = TryFindLogicalGet(node);

    auto op = std::make_unique<PhysicalDelete>();
    op->schema_name  = node.schema_name;
    op->table_name   = node.table_name;
    op->output_types = {};
    op->output_names = {};

    // Extract filter predicate from child filter node (if any).
    if (!node.children.empty()) {
        if (const auto* filt = dynamic_cast<const LogicalFilter*>(node.children[0].get())) {
            if (filt->predicate) {
                if (get) {
                    op->predicate = RemapColumnRefs(CloneExpr(*filt->predicate),
                                                    get->column_ids);
                } else {
                    op->predicate = CloneExpr(*filt->predicate);
                }
            }
        }
    }

    return op;
}

std::unique_ptr<PhysicalOperator> PhysicalPlanner::PlanUpdate(const LogicalUpdate& node) {
    const LogicalGet* get = TryFindLogicalGet(node);

    auto op = std::make_unique<PhysicalUpdate>();
    op->schema_name    = node.schema_name;
    op->table_name     = node.table_name;
    op->update_col_ids = node.update_col_ids;
    if (get) {
        op->scan_col_ids = get->column_ids;
    } else {
        op->scan_col_ids = node.update_col_ids;
    }
    op->output_types   = {};
    op->output_names   = {};

    // Clone update expressions.
    for (const auto& e : node.update_exprs) {
        if (e) {
            if (get) {
                op->update_exprs.push_back(RemapColumnRefs(CloneExpr(*e), get->column_ids));
            } else {
                op->update_exprs.push_back(CloneExpr(*e));
            }
        }
    }

    // Extract filter predicate from child filter node (if any).
    if (!node.children.empty()) {
        if (const auto* filt = dynamic_cast<const LogicalFilter*>(node.children[0].get())) {
            if (filt->predicate) {
                if (get) {
                    op->predicate = RemapColumnRefs(CloneExpr(*filt->predicate),
                                                    get->column_ids);
                } else {
                    op->predicate = CloneExpr(*filt->predicate);
                }
            }
        }
    }

    return op;
}

} // namespace cppcoldb
