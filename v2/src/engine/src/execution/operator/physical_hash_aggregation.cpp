#include "cppcoldb/engine/execution/operator/physical_hash_aggregation.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::execution {

std::unique_ptr<OperatorState> PhysicalHashAggregation::CreateSinkState() const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void PhysicalHashAggregation::Consume(const common::DataChunk& input, OperatorState& state,
                                       ::cppcoldb::ClientContext& ctx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void PhysicalHashAggregation::Finalize(OperatorState& state, ::cppcoldb::ClientContext& ctx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::execution
