#include "execution/operator/expr_evaluator.hpp"
#include "planner/logical_plan/logical_plan.hpp"
#include "common/types.hpp"
#include "common/exception.hpp"

namespace cppcoldb {

namespace {

static void ApplyBinaryOp(const std::string& op,
                           const DataVector& left, const DataVector& right,
                           size_t count, TypeId result_type, DataVector& output) {
    output.Reset(result_type, count);

    bool is_arithmetic = (op == "+" || op == "-" || op == "*" || op == "/");
    bool is_comparison = (op == "=" || op == "<" || op == ">" ||
                          op == "<=" || op == ">=" || op == "<>");
    bool is_logical = (op == "AND" || op == "OR");

    for (size_t i = 0; i < count; ++i) {
        // NULL propagation
        if (left.IsNull(i) || right.IsNull(i)) {
            output.SetNull(i);
            continue;
        }

        if (is_arithmetic) {
            double lv, rv;
            if (left.type == TypeId::FLOAT64 || left.type == TypeId::FLOAT32)
                lv = left.float_data[i];
            else
                lv = static_cast<double>(left.int_data[i]);
            if (right.type == TypeId::FLOAT64 || right.type == TypeId::FLOAT32)
                rv = right.float_data[i];
            else
                rv = static_cast<double>(right.int_data[i]);

            double res;
            if (op == "+") res = lv + rv;
            else if (op == "-") res = lv - rv;
            else if (op == "*") res = lv * rv;
            else { // "/"
                if (rv == 0.0) { output.SetNull(i); continue; }
                res = lv / rv;
            }

            if (result_type == TypeId::FLOAT64 || result_type == TypeId::FLOAT32) {
                output.float_data[i] = res;
            } else {
                output.int_data[i] = static_cast<int64_t>(res);
            }
            output.validity.set(i);

        } else if (is_comparison) {
            bool result;
            bool left_is_float  = (left.type  == TypeId::FLOAT64 || left.type  == TypeId::FLOAT32);
            bool right_is_float = (right.type == TypeId::FLOAT64 || right.type == TypeId::FLOAT32);

            if (left.type == TypeId::VARCHAR && right.type == TypeId::VARCHAR) {
                const auto& ls = left.str_data[i];
                const auto& rs = right.str_data[i];
                if (op == "=")  result = (ls == rs);
                else if (op == "<>") result = (ls != rs);
                else if (op == "<")  result = (ls <  rs);
                else if (op == ">")  result = (ls >  rs);
                else if (op == "<=") result = (ls <= rs);
                else                 result = (ls >= rs);
            } else if (left_is_float || right_is_float) {
                double lv = left_is_float  ? left.float_data[i]  : static_cast<double>(left.int_data[i]);
                double rv = right_is_float ? right.float_data[i] : static_cast<double>(right.int_data[i]);
                if (op == "=")  result = (lv == rv);
                else if (op == "<>") result = (lv != rv);
                else if (op == "<")  result = (lv <  rv);
                else if (op == ">")  result = (lv >  rv);
                else if (op == "<=") result = (lv <= rv);
                else                 result = (lv >= rv);
            } else {
                int64_t li = left.int_data[i], ri = right.int_data[i];
                if (op == "=")  result = (li == ri);
                else if (op == "<>") result = (li != ri);
                else if (op == "<")  result = (li <  ri);
                else if (op == ">")  result = (li >  ri);
                else if (op == "<=") result = (li <= ri);
                else                 result = (li >= ri);
            }
            output.int_data[i] = result ? 1 : 0;
            output.validity.set(i);

        } else if (is_logical) {
            bool lv = (left.int_data[i] != 0);
            bool rv = (right.int_data[i] != 0);
            bool res = (op == "AND") ? (lv && rv) : (lv || rv);
            output.int_data[i] = res ? 1 : 0;
            output.validity.set(i);
        }
    }
}

static void ApplyUnaryOp(const std::string& op, const DataVector& child,
                          size_t count, DataVector& output) {
    output.Reset(child.type, count);
    for (size_t i = 0; i < count; ++i) {
        if (child.IsNull(i)) { output.SetNull(i); continue; }
        if (op == "NOT") {
            output.int_data[i] = (child.int_data[i] == 0) ? 1 : 0;
        } else if (op == "-") {
            if (child.type == TypeId::FLOAT64 || child.type == TypeId::FLOAT32)
                output.float_data[i] = -child.float_data[i];
            else
                output.int_data[i] = -child.int_data[i];
        }
        output.validity.set(i);
    }
}

static void ApplyCast(const DataVector& src, TypeId from_type, TypeId to_type,
                       size_t count, DataVector& output) {
    output.Reset(to_type, count);
    for (size_t i = 0; i < count; ++i) {
        if (src.IsNull(i)) { output.SetNull(i); continue; }
        if (from_type == TypeId::INT64 && (to_type == TypeId::FLOAT64 || to_type == TypeId::FLOAT32)) {
            output.float_data[i] = static_cast<double>(src.int_data[i]);
        } else if ((from_type == TypeId::FLOAT64 || from_type == TypeId::FLOAT32) && to_type == TypeId::INT64) {
            output.int_data[i] = static_cast<int64_t>(src.float_data[i]);
        } else {
            // Same type or unhandled: copy
            output = src; return;
        }
        output.validity.set(i);
    }
}

} // anonymous namespace

void ExprEvaluator::Evaluate(const LogicalExpr& expr, const DataChunk& chunk, DataVector& output) {
    switch (expr.kind) {
    case LogicalExpr::Kind::BOUND_COLUMN_REF: {
        auto& ref = static_cast<const BoundColumnRef&>(expr);
        output = chunk.columns[ref.column_idx];  // copy the DataVector
        break;
    }
    case LogicalExpr::Kind::LITERAL: {
        auto& lit = static_cast<const LogicalLit&>(expr);
        if (lit.is_null) {
            output.Reset(expr.result_type == TypeId::INVALID ? TypeId::INT64 : expr.result_type, chunk.count);
            // Leave all validity bits unset (null)
        } else {
            DataVectorFill(output, lit.value, chunk.count);
        }
        break;
    }
    case LogicalExpr::Kind::BINARY_OP: {
        auto& bop = static_cast<const LogicalBinaryOp&>(expr);
        DataVector left_vec, right_vec;
        Evaluate(*bop.left,  chunk, left_vec);
        Evaluate(*bop.right, chunk, right_vec);
        ApplyBinaryOp(bop.op, left_vec, right_vec, chunk.count, expr.result_type, output);
        break;
    }
    case LogicalExpr::Kind::UNARY_OP: {
        auto& uop = static_cast<const LogicalUnaryOp&>(expr);
        DataVector child_vec;
        Evaluate(*uop.child, chunk, child_vec);
        ApplyUnaryOp(uop.op, child_vec, chunk.count, output);
        break;
    }
    case LogicalExpr::Kind::CAST: {
        auto& cast = static_cast<const LogicalCast&>(expr);
        DataVector child_vec;
        Evaluate(*cast.child, chunk, child_vec);
        ApplyCast(child_vec, cast.from_type, expr.result_type, chunk.count, output);
        break;
    }
    case LogicalExpr::Kind::AGGREGATE:
        throw RuntimeError("aggregate expression cannot be evaluated in scalar context");
    default:
        throw RuntimeError("unknown expression kind in ExprEvaluator");
    }
}

std::vector<uint16_t> ExprEvaluator::EvaluatePredicate(const LogicalExpr& pred, const DataChunk& chunk) {
    DataVector result;
    Evaluate(pred, chunk, result);
    std::vector<uint16_t> selection;
    selection.reserve(chunk.count);
    for (size_t i = 0; i < chunk.count; ++i) {
        if (!result.IsNull(i) && result.int_data[i] != 0) {
            selection.push_back(static_cast<uint16_t>(i));
        }
    }
    return selection;
}

void DataChunkCompact(const DataChunk& src, const std::vector<uint16_t>& selection, DataChunk& dst) {
    std::vector<TypeId> types;
    for (auto& col : src.columns) types.push_back(col.type);
    dst.Initialize(types);

    std::vector<uint32_t> idx32(selection.begin(), selection.end());
    DataChunkSlice(dst, src, idx32);
}

} // namespace cppcoldb
