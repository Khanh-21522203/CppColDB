#pragma once
#include <vector>
#include "planner/logical_plan/logical_plan.hpp"
#include "common/types.hpp"

namespace cppcoldb {

class ExprEvaluator {
public:
    // Evaluate expr for every row in chunk; write result into output.
    // output must already be Reset() to the correct type by the caller before this call.
    static void Evaluate(const LogicalExpr& expr, const DataChunk& chunk, DataVector& output);

    // Evaluate a boolean predicate; return indices of rows that pass.
    static std::vector<uint16_t> EvaluatePredicate(const LogicalExpr& pred, const DataChunk& chunk);
};

// Copy only the rows at selection indices from src into dst.
// dst must be uninitialized (Initialize will be called inside).
void DataChunkCompact(const DataChunk& src,
                      const std::vector<uint16_t>& selection,
                      DataChunk& dst);

} // namespace cppcoldb
