#pragma once
#include "execution/physical_operator.hpp"
#include "execution/pipeline.hpp"

namespace cppcoldb {

struct LimitState : OperatorState {
    int64_t rows_emitted = 0;
    int64_t rows_skipped = 0;
};

struct PhysicalLimit : PhysicalOperator {
    int64_t limit  = -1;
    int64_t offset =  0;

    PhysicalLimit() { role = OperatorRole::OPERATOR; }

    std::unique_ptr<OperatorState> CreateOperatorState() const override;
    OperatorResultType Execute(const DataChunk& input, DataChunk& output,
                               OperatorState& state, ClientContext& ctx) override;
};

} // namespace cppcoldb
