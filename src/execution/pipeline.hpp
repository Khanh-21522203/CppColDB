#pragma once
#include <vector>
#include <memory>
#include "execution/physical_operator.hpp"

namespace cppcoldb {

// Per-operator local state (subclassed by operators that need private cursors).
struct OperatorState {
    virtual ~OperatorState() = default;
};

// Scan source cursor.
struct ScanState : OperatorState {
    size_t current_row_group = 0;
    size_t row_offset        = 0;
};

// A linear execution chain: one source → N operators → one sink.
struct Pipeline {
    PhysicalOperator*              source = nullptr;
    std::vector<PhysicalOperator*> operators;
    PhysicalOperator*              sink   = nullptr;

    // Pipelines this one depends on (must complete first — e.g. hash join build).
    std::vector<Pipeline*> dependencies;
};

} // namespace cppcoldb
