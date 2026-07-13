#include "cppcoldb/engine/execution/operator/expr_evaluator.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::execution {

void ExprEvaluator::Evaluate(const Expression& expr, const common::DataChunk& chunk,
                              common::DataVector& output) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

std::vector<std::uint16_t> ExprEvaluator::EvaluatePredicate(const Expression& pred,
                                                              const common::DataChunk& chunk) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void DataChunkCompact(const common::DataChunk& src, const std::vector<std::uint16_t>& selection,
                      common::DataChunk& dst) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::execution
