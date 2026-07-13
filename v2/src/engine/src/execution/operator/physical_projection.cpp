#include "cppcoldb/engine/execution/operator/physical_projection.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::execution {

OperatorResultType PhysicalProjection::Execute(const common::DataChunk& input, common::DataChunk& output,
                                                OperatorState& state, ::cppcoldb::ClientContext& ctx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::execution
