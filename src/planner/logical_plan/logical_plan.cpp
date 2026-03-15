#include "planner/logical_plan/logical_plan.hpp"
#include "common/exception.hpp"

namespace cppcoldb {

std::unique_ptr<LogicalExpr> CloneExpr(const LogicalExpr& e) {
    switch (e.kind) {
        case LogicalExpr::Kind::BOUND_COLUMN_REF: {
            const auto& src = static_cast<const BoundColumnRef&>(e);
            auto dst        = std::make_unique<BoundColumnRef>();
            dst->result_type = src.result_type;
            dst->column_idx  = src.column_idx;
            dst->name        = src.name;
            return dst;
        }
        case LogicalExpr::Kind::LITERAL: {
            const auto& src = static_cast<const LogicalLit&>(e);
            auto dst        = std::make_unique<LogicalLit>();
            dst->result_type = src.result_type;
            dst->value       = src.value;
            dst->is_null     = src.is_null;
            return dst;
        }
        case LogicalExpr::Kind::BINARY_OP: {
            const auto& src = static_cast<const LogicalBinaryOp&>(e);
            auto dst        = std::make_unique<LogicalBinaryOp>();
            dst->result_type = src.result_type;
            dst->op          = src.op;
            dst->left        = CloneExpr(*src.left);
            dst->right       = CloneExpr(*src.right);
            return dst;
        }
        case LogicalExpr::Kind::UNARY_OP: {
            const auto& src = static_cast<const LogicalUnaryOp&>(e);
            auto dst        = std::make_unique<LogicalUnaryOp>();
            dst->result_type = src.result_type;
            dst->op          = src.op;
            dst->child       = CloneExpr(*src.child);
            return dst;
        }
        case LogicalExpr::Kind::CAST: {
            const auto& src = static_cast<const LogicalCast&>(e);
            auto dst        = std::make_unique<LogicalCast>();
            dst->result_type = src.result_type;
            dst->from_type   = src.from_type;
            dst->child       = CloneExpr(*src.child);
            return dst;
        }
        case LogicalExpr::Kind::AGGREGATE: {
            const auto& src = static_cast<const LogicalAggrExpr&>(e);
            auto dst        = std::make_unique<LogicalAggrExpr>();
            dst->result_type = src.result_type;
            dst->func        = src.func;
            dst->is_star     = src.is_star;
            if (src.arg) {
                dst->arg = CloneExpr(*src.arg);
            }
            return dst;
        }
    }
    throw RuntimeError("CloneExpr: unknown LogicalExpr::Kind");
}

} // namespace cppcoldb
