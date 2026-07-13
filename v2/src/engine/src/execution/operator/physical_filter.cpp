#include "cppcoldb/engine/execution/operator/physical_filter.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::execution {

OperatorResultType PhysicalFilter::Execute(const common::DataChunk& input, common::DataChunk& output,
                                            OperatorState& state, ::cppcoldb::ClientContext& ctx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::execution
