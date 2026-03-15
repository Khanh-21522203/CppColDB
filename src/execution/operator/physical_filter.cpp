#include "execution/operator/physical_filter.hpp"
#include "execution/operator/expr_evaluator.hpp"
#include "main/client_context.hpp"

namespace cppcoldb {

OperatorResultType PhysicalFilter::Execute(const DataChunk& input, DataChunk& output,
                                            OperatorState& /*state*/, ClientContext& /*ctx*/) {
    auto selection = ExprEvaluator::EvaluatePredicate(*predicate, input);
    if (!selection.empty()) {
        DataChunkCompact(input, selection, output);
    }
    return OperatorResultType::NEED_MORE_INPUT;
}

} // namespace cppcoldb
