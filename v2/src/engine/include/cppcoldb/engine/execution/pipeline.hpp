#pragma once

#include <vector>

#include "cppcoldb/engine/execution/physical_operator.hpp"

namespace cppcoldb::engine::execution {

// One linear source -> operators -> sink chain of a physical plan, plus the
// other Pipelines it must wait on (e.g. the build side of a hash join).
// Pipelines and the PhysicalOperators they reference are owned by the
// Executor; a Pipeline itself only observes them via non-owning pointers.
struct Pipeline {
    PhysicalOperator*              source = nullptr;
    std::vector<PhysicalOperator*> operators;
    PhysicalOperator*              sink = nullptr;
    std::vector<Pipeline*>         dependencies;
};

} // namespace cppcoldb::engine::execution
