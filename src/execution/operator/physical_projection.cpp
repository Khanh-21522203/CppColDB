#include "execution/operator/physical_projection.hpp"
#include "execution/operator/expr_evaluator.hpp"
#include "main/client_context.hpp"

namespace cppcoldb {

OperatorResultType PhysicalProjection::Execute(const DataChunk& input, DataChunk& output,
                                                OperatorState& /*state*/, ClientContext& /*ctx*/) {
    std::vector<TypeId> types;
    types.reserve(exprs.size());
    for (const auto& e : exprs) types.push_back(e->result_type);

    output.Initialize(types);
    output.count = input.count;
    for (size_t i = 0; i < exprs.size(); ++i) {
        ExprEvaluator::Evaluate(*exprs[i], input, output.columns[i]);
    }
    return OperatorResultType::NEED_MORE_INPUT;
}

} // namespace cppcoldb
