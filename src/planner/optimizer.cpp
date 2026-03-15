#include "planner/optimizer.hpp"
#include "common/exception.hpp"
#include <set>
#include <cmath>

namespace cppcoldb {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

Optimizer::Optimizer(OptimizerConfig cfg) : cfg_(cfg) {}

// ---------------------------------------------------------------------------
// IsTrivialPlan
// ---------------------------------------------------------------------------

bool Optimizer::IsTrivialPlan(const LogicalPlan& plan) {
    switch (plan.node_type) {
        case LogicalPlan::Type::CREATE_TABLE:
        case LogicalPlan::Type::DROP_TABLE:
        case LogicalPlan::Type::INSERT:
        case LogicalPlan::Type::UPDATE:
        case LogicalPlan::Type::DELETE:
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Optimize — entry point
// ---------------------------------------------------------------------------

std::unique_ptr<LogicalPlan> Optimizer::Optimize(std::unique_ptr<LogicalPlan> plan) {
    if (IsTrivialPlan(*plan)) return plan;

    if (cfg_.constant_folding)   plan = RunConstantFolding  (std::move(plan));
    if (cfg_.predicate_pushdown) plan = RunPredicatePushdown(std::move(plan));
    if (cfg_.column_pruning)     plan = RunColumnPruning    (std::move(plan));
    if (cfg_.filter_merge)       plan = RunFilterMerge      (std::move(plan));

    return plan;
}

// ---------------------------------------------------------------------------
// Pass 1 — Constant Folding
// ---------------------------------------------------------------------------

static bool IsNumericLit(const LogicalLit& lit) {
    return !lit.is_null &&
           (lit.result_type == TypeId::INT64  ||
            lit.result_type == TypeId::INT32  ||
            lit.result_type == TypeId::INT16  ||
            lit.result_type == TypeId::INT8   ||
            lit.result_type == TypeId::FLOAT64||
            lit.result_type == TypeId::FLOAT32);
}

static bool IsBoolLit(const LogicalLit& lit) {
    return !lit.is_null && lit.result_type == TypeId::BOOLEAN;
}

static double LitToDouble(const LogicalLit& lit) {
    if (lit.result_type == TypeId::FLOAT64 || lit.result_type == TypeId::FLOAT32) {
        return lit.value.GetFloat64();
    }
    return static_cast<double>(lit.value.GetInt64());
}

static std::unique_ptr<LogicalLit> MakeIntLit(int64_t v) {
    auto l = std::make_unique<LogicalLit>();
    l->result_type = TypeId::INT64;
    l->value = Value::Integer(v);
    return l;
}

static std::unique_ptr<LogicalLit> MakeFloatLit(double v) {
    auto l = std::make_unique<LogicalLit>();
    l->result_type = TypeId::FLOAT64;
    l->value = Value::Float(v);
    return l;
}

static std::unique_ptr<LogicalLit> MakeBoolLit(bool v) {
    auto l = std::make_unique<LogicalLit>();
    l->result_type = TypeId::BOOLEAN;
    l->value = Value::Boolean(v);
    return l;
}

std::unique_ptr<LogicalExpr> Optimizer::FoldExpr(std::unique_ptr<LogicalExpr> e) {
    if (!e) return e;

    switch (e->kind) {
        case LogicalExpr::Kind::LITERAL:
            return e;  // already folded

        case LogicalExpr::Kind::BINARY_OP: {
            auto& b = static_cast<LogicalBinaryOp&>(*e);
            b.left  = FoldExpr(std::move(b.left));
            b.right = FoldExpr(std::move(b.right));

            // Only fold if both children are non-null literals
            if (b.left->kind  != LogicalExpr::Kind::LITERAL ||
                b.right->kind != LogicalExpr::Kind::LITERAL) {
                return e;
            }
            auto& llit = static_cast<LogicalLit&>(*b.left);
            auto& rlit = static_cast<LogicalLit&>(*b.right);

            // Don't fold nulls
            if (llit.is_null || rlit.is_null) return e;

            const std::string& op = b.op;

            // Arithmetic on numerics
            if (op == "+" || op == "-" || op == "*" || op == "/") {
                if (!IsNumericLit(llit) || !IsNumericLit(rlit)) return e;

                bool is_float = (llit.result_type == TypeId::FLOAT64 ||
                                 llit.result_type == TypeId::FLOAT32 ||
                                 rlit.result_type == TypeId::FLOAT64 ||
                                 rlit.result_type == TypeId::FLOAT32);

                if (is_float) {
                    double lv = LitToDouble(llit);
                    double rv = LitToDouble(rlit);
                    double result = 0.0;
                    if      (op == "+") result = lv + rv;
                    else if (op == "-") result = lv - rv;
                    else if (op == "*") result = lv * rv;
                    else if (op == "/") {
                        if (rv == 0.0) return e;  // don't fold div-by-zero
                        result = lv / rv;
                    }
                    return MakeFloatLit(result);
                } else {
                    int64_t lv = llit.value.GetInt64();
                    int64_t rv = rlit.value.GetInt64();
                    int64_t result = 0;
                    if      (op == "+") result = lv + rv;
                    else if (op == "-") result = lv - rv;
                    else if (op == "*") result = lv * rv;
                    else if (op == "/") {
                        if (rv == 0) return e;  // don't fold div-by-zero
                        result = lv / rv;
                    }
                    return MakeIntLit(result);
                }
            }

            // Numeric comparisons
            if (op == "=" || op == "<" || op == ">" ||
                op == "<=" || op == ">=" || op == "<>") {
                if (!IsNumericLit(llit) || !IsNumericLit(rlit)) {
                    // Attempt string or bool equality
                    if (op == "=" && !IsBoolLit(llit) && !IsBoolLit(rlit)) {
                        // String compare
                        bool eq = (llit.value == rlit.value);
                        return MakeBoolLit(op == "=" ? eq : !eq);
                    }
                    return e;
                }
                bool is_float = (llit.result_type == TypeId::FLOAT64 ||
                                 llit.result_type == TypeId::FLOAT32 ||
                                 rlit.result_type == TypeId::FLOAT64 ||
                                 rlit.result_type == TypeId::FLOAT32);
                bool result = false;
                if (is_float) {
                    double lv = LitToDouble(llit);
                    double rv = LitToDouble(rlit);
                    if      (op == "=")  result = (lv == rv);
                    else if (op == "<")  result = (lv <  rv);
                    else if (op == ">")  result = (lv >  rv);
                    else if (op == "<=") result = (lv <= rv);
                    else if (op == ">=") result = (lv >= rv);
                    else if (op == "<>") result = (lv != rv);
                } else {
                    int64_t lv = llit.value.GetInt64();
                    int64_t rv = rlit.value.GetInt64();
                    if      (op == "=")  result = (lv == rv);
                    else if (op == "<")  result = (lv <  rv);
                    else if (op == ">")  result = (lv >  rv);
                    else if (op == "<=") result = (lv <= rv);
                    else if (op == ">=") result = (lv >= rv);
                    else if (op == "<>") result = (lv != rv);
                }
                return MakeBoolLit(result);
            }

            // Boolean AND / OR
            if (op == "AND" || op == "OR") {
                if (!IsBoolLit(llit) || !IsBoolLit(rlit)) return e;
                bool lv = llit.value.GetBool();
                bool rv = rlit.value.GetBool();
                bool result = (op == "AND") ? (lv && rv) : (lv || rv);
                return MakeBoolLit(result);
            }

            return e;
        }

        case LogicalExpr::Kind::UNARY_OP: {
            auto& u = static_cast<LogicalUnaryOp&>(*e);
            u.child = FoldExpr(std::move(u.child));

            if (u.child->kind != LogicalExpr::Kind::LITERAL) return e;
            auto& lit = static_cast<LogicalLit&>(*u.child);
            if (lit.is_null) return e;

            if (u.op == "NOT" && IsBoolLit(lit)) {
                return MakeBoolLit(!lit.value.GetBool());
            }
            if (u.op == "-" && IsNumericLit(lit)) {
                bool is_float = (lit.result_type == TypeId::FLOAT64 ||
                                 lit.result_type == TypeId::FLOAT32);
                if (is_float) return MakeFloatLit(-LitToDouble(lit));
                return MakeIntLit(-lit.value.GetInt64());
            }
            return e;
        }

        default:
            return e;
    }
}

std::unique_ptr<LogicalPlan> Optimizer::FoldPlan(std::unique_ptr<LogicalPlan> plan) {
    if (!plan) return plan;

    // Recurse into children first
    for (auto& child : plan->children) {
        child = FoldPlan(std::move(child));
    }

    if (plan->node_type == LogicalPlan::Type::FILTER) {
        auto& f = static_cast<LogicalFilter&>(*plan);
        f.predicate = FoldExpr(std::move(f.predicate));

        // predicate is LITERAL BOOLEAN true → remove filter, return child
        if (f.predicate->kind == LogicalExpr::Kind::LITERAL) {
            auto& lit = static_cast<LogicalLit&>(*f.predicate);
            if (!lit.is_null && lit.result_type == TypeId::BOOLEAN) {
                if (lit.value.GetBool()) {
                    // Always true — replace filter with its child
                    return std::move(plan->children[0]);
                } else {
                    // Always false — replace with empty LogicalGet
                    auto empty = std::make_unique<LogicalGet>();
                    empty->schema_name       = "";
                    empty->table_name        = "__empty__";
                    empty->catalog_row_count = 0;
                    empty->output_types      = plan->output_types;
                    empty->output_names      = plan->output_names;
                    return empty;
                }
            }
        }
    }

    // Fold expressions in projections
    if (plan->node_type == LogicalPlan::Type::PROJECTION) {
        auto& p = static_cast<LogicalProjection&>(*plan);
        for (auto& expr : p.exprs) {
            expr = FoldExpr(std::move(expr));
        }
    }

    return plan;
}

std::unique_ptr<LogicalPlan> Optimizer::RunConstantFolding(std::unique_ptr<LogicalPlan> plan) {
    return FoldPlan(std::move(plan));
}

// ---------------------------------------------------------------------------
// Pass 2 — Predicate Pushdown
// ---------------------------------------------------------------------------

static std::unique_ptr<LogicalExpr> CombinePredicates(
    std::vector<std::unique_ptr<LogicalExpr>>& pending) {
    if (pending.empty()) return nullptr;
    if (pending.size() == 1) return std::move(pending[0]);

    // Chain into AND tree (left-associative)
    auto result = std::move(pending[0]);
    for (size_t i = 1; i < pending.size(); ++i) {
        auto and_node       = std::make_unique<LogicalBinaryOp>();
        and_node->op        = "AND";
        and_node->result_type = TypeId::BOOLEAN;
        and_node->left      = std::move(result);
        and_node->right     = std::move(pending[i]);
        result = std::move(and_node);
    }
    return result;
}

std::unique_ptr<LogicalPlan> Optimizer::Pushdown(
    std::unique_ptr<LogicalPlan> node,
    std::vector<std::unique_ptr<LogicalExpr>>& pending) {

    if (!node) return node;

    switch (node->node_type) {
        case LogicalPlan::Type::GET: {
            // Push all pending predicates into pushed_filters
            auto& get = static_cast<LogicalGet&>(*node);
            for (auto& p : pending) {
                get.pushed_filters.push_back(std::move(p));
            }
            pending.clear();
            return node;
        }

        case LogicalPlan::Type::FILTER: {
            // Extract this filter's predicate into pending, recurse into child
            auto& f = static_cast<LogicalFilter&>(*node);
            pending.push_back(std::move(f.predicate));
            // Recurse into child
            node->children[0] = Pushdown(std::move(node->children[0]), pending);
            // If all predicates were pushed (pending is empty), return child directly
            if (pending.empty()) {
                return std::move(node->children[0]);
            }
            // Re-attach remaining predicates as a new filter on top
            auto combined = CombinePredicates(pending);
            auto new_filter = std::make_unique<LogicalFilter>();
            new_filter->predicate    = std::move(combined);
            new_filter->output_types = node->output_types;
            new_filter->output_names = node->output_names;
            new_filter->children.push_back(std::move(node->children[0]));
            return new_filter;
        }

        default: {
            // For Projection, Limit, Sort, Aggregate: recurse into children
            for (auto& child : node->children) {
                child = Pushdown(std::move(child), pending);
            }
            // If there are still pending predicates not absorbed by a GET below,
            // wrap this node in a LogicalFilter
            if (!pending.empty()) {
                auto combined = CombinePredicates(pending);
                auto new_filter = std::make_unique<LogicalFilter>();
                new_filter->predicate    = std::move(combined);
                new_filter->output_types = node->output_types;
                new_filter->output_names = node->output_names;
                new_filter->children.push_back(std::move(node));
                return new_filter;
            }
            return node;
        }
    }
}

std::unique_ptr<LogicalPlan> Optimizer::RunPredicatePushdown(std::unique_ptr<LogicalPlan> plan) {
    std::vector<std::unique_ptr<LogicalExpr>> pending;
    return Pushdown(std::move(plan), pending);
}

// ---------------------------------------------------------------------------
// Pass 3 — Column Pruning
// ---------------------------------------------------------------------------

void Optimizer::CollectColumnRefs(const LogicalExpr& e, std::set<size_t>& out) {
    switch (e.kind) {
        case LogicalExpr::Kind::BOUND_COLUMN_REF: {
            const auto& ref = static_cast<const BoundColumnRef&>(e);
            out.insert(ref.column_idx);
            break;
        }
        case LogicalExpr::Kind::BINARY_OP: {
            const auto& b = static_cast<const LogicalBinaryOp&>(e);
            if (b.left)  CollectColumnRefs(*b.left,  out);
            if (b.right) CollectColumnRefs(*b.right, out);
            break;
        }
        case LogicalExpr::Kind::UNARY_OP: {
            const auto& u = static_cast<const LogicalUnaryOp&>(e);
            if (u.child) CollectColumnRefs(*u.child, out);
            break;
        }
        case LogicalExpr::Kind::CAST: {
            const auto& c = static_cast<const LogicalCast&>(e);
            if (c.child) CollectColumnRefs(*c.child, out);
            break;
        }
        case LogicalExpr::Kind::AGGREGATE: {
            const auto& a = static_cast<const LogicalAggrExpr&>(e);
            if (a.arg) CollectColumnRefs(*a.arg, out);
            break;
        }
        case LogicalExpr::Kind::LITERAL:
        default:
            break;
    }
}

std::unique_ptr<LogicalPlan> Optimizer::PruneColumns(
    std::unique_ptr<LogicalPlan> plan,
    std::set<size_t>& required_cols) {

    if (!plan) return plan;

    switch (plan->node_type) {
        case LogicalPlan::Type::GET: {
            auto& get = static_cast<LogicalGet&>(*plan);

            if (required_cols.empty()) {
                // Safety: nothing to prune if no requirements
                return plan;
            }

            // Save originals
            auto old_col_ids   = get.column_ids;
            auto old_types     = get.output_types;
            auto old_names     = get.output_names;

            // Rebuild keeping only columns in required_cols
            std::vector<size_t>      new_col_ids;
            std::vector<TypeId>      new_types;
            std::vector<std::string> new_names;

            for (size_t i = 0; i < old_col_ids.size(); ++i) {
                if (required_cols.count(old_col_ids[i])) {
                    new_col_ids.push_back(old_col_ids[i]);
                    if (i < old_types.size()) new_types.push_back(old_types[i]);
                    if (i < old_names.size()) new_names.push_back(old_names[i]);
                }
            }

            // Must keep at least one column
            if (new_col_ids.empty() && !old_col_ids.empty()) {
                new_col_ids.push_back(old_col_ids[0]);
                if (!old_types.empty()) new_types.push_back(old_types[0]);
                if (!old_names.empty()) new_names.push_back(old_names[0]);
            }

            get.column_ids   = std::move(new_col_ids);
            get.output_types = std::move(new_types);
            get.output_names = std::move(new_names);
            return plan;
        }

        case LogicalPlan::Type::FILTER: {
            auto& f = static_cast<LogicalFilter&>(*plan);
            // Add columns referenced by the predicate
            if (f.predicate) CollectColumnRefs(*f.predicate, required_cols);
            f.children[0] = PruneColumns(std::move(f.children[0]), required_cols);
            return plan;
        }

        case LogicalPlan::Type::PROJECTION: {
            auto& p = static_cast<LogicalProjection&>(*plan);
            // Seed required_cols from all column refs in exprs
            for (auto& expr : p.exprs) {
                if (expr) CollectColumnRefs(*expr, required_cols);
            }
            p.children[0] = PruneColumns(std::move(p.children[0]), required_cols);
            return plan;
        }

        case LogicalPlan::Type::LIMIT:
        case LogicalPlan::Type::SORT:
        case LogicalPlan::Type::AGGREGATE: {
            // Pass required_cols through unchanged
            for (auto& child : plan->children) {
                child = PruneColumns(std::move(child), required_cols);
            }
            return plan;
        }

        default:
            return plan;
    }
}

std::unique_ptr<LogicalPlan> Optimizer::RunColumnPruning(std::unique_ptr<LogicalPlan> plan) {
    std::set<size_t> required_cols;
    return PruneColumns(std::move(plan), required_cols);
}

// ---------------------------------------------------------------------------
// Pass 4 — Filter Merge
// ---------------------------------------------------------------------------

std::unique_ptr<LogicalPlan> Optimizer::MergeFilters(std::unique_ptr<LogicalPlan> plan) {
    if (!plan) return plan;

    // Bottom-up: recurse into children first
    for (auto& child : plan->children) {
        child = MergeFilters(std::move(child));
    }

    // If this is a LogicalFilter whose child is also a LogicalFilter, merge them
    if (plan->node_type == LogicalPlan::Type::FILTER &&
        !plan->children.empty() &&
        plan->children[0]->node_type == LogicalPlan::Type::FILTER) {

        auto& outer = static_cast<LogicalFilter&>(*plan);
        auto inner_plan = std::move(plan->children[0]);
        auto& inner = static_cast<LogicalFilter&>(*inner_plan);

        // outer.predicate AND inner.predicate
        auto combined       = std::make_unique<LogicalBinaryOp>();
        combined->op        = "AND";
        combined->result_type = TypeId::BOOLEAN;
        combined->left      = std::move(outer.predicate);
        combined->right     = std::move(inner.predicate);

        outer.predicate = std::move(combined);
        // Adopt inner's children
        plan->children = std::move(inner_plan->children);
        return plan;
    }

    return plan;
}

std::unique_ptr<LogicalPlan> Optimizer::RunFilterMerge(std::unique_ptr<LogicalPlan> plan) {
    return MergeFilters(std::move(plan));
}

} // namespace cppcoldb
