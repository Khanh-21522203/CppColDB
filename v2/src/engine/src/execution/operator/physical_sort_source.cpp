#include "cppcoldb/engine/execution/operator/physical_sort_source.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::execution {

std::unique_ptr<OperatorState> PhysicalSortSource::CreateScanState() const { CPPCOLDB_NOT_IMPLEMENTED(); }

OperatorResultType PhysicalSortSource::GetData(OperatorState& state, common::DataChunk& output,
                                                ::cppcoldb::ClientContext& ctx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::execution
