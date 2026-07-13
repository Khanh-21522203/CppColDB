#pragma once

#include <cstdint>

#include "cppcoldb/engine/execution/physical_operator.hpp"

namespace cppcoldb::engine::execution {

// Tracks how many rows have been skipped for OFFSET and emitted for LIMIT so far.
struct LimitState : public OperatorState {
    std::int64_t rows_emitted = 0;
    std::int64_t rows_skipped = 0;
};

// OPERATOR: passes through rows after skipping `offset` of them, and stops
// producing output (signals FINISHED) once `limit` rows have been emitted.
// `limit == -1` means unbounded.
class PhysicalLimit : public PhysicalOperator {
public:
    PhysicalLimit() { role = OperatorRole::OPERATOR; }

    std::unique_ptr<OperatorState> CreateOperatorState() const override;
    OperatorResultType Execute(const common::DataChunk& input, common::DataChunk& output,
                                OperatorState& state, ::cppcoldb::ClientContext& ctx) override;

    std::int64_t limit = -1;
    std::int64_t offset = 0;
};

} // namespace cppcoldb::engine::execution
