#include "cppcoldb/engine/execution/operator/physical_sort.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::execution {

std::unique_ptr<OperatorState> PhysicalSort::CreateSinkState() const { CPPCOLDB_NOT_IMPLEMENTED(); }

void PhysicalSort::Consume(const common::DataChunk& input, OperatorState& state,
                            ::cppcoldb::ClientContext& ctx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void PhysicalSort::Finalize(OperatorState& state, ::cppcoldb::ClientContext& ctx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::execution
