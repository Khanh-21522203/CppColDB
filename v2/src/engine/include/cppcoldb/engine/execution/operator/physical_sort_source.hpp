#pragma once

#include <cstddef>
#include <memory>

#include "cppcoldb/engine/execution/operator/physical_sort.hpp"
#include "cppcoldb/engine/execution/physical_operator.hpp"

namespace cppcoldb::engine::execution {

struct SortSourceState : public OperatorState {
    std::size_t chunk_idx = 0;
    std::size_t row_idx = 0;
};

// SOURCE: streams rows back out of a SortBuffer in the order PhysicalSort's
// Finalize() computed, one output chunk at a time.
class PhysicalSortSource : public PhysicalOperator {
public:
    PhysicalSortSource() { role = OperatorRole::SOURCE; }

    std::unique_ptr<OperatorState> CreateScanState() const override;
    void InitScan(OperatorState& state, ::cppcoldb::ClientContext& ctx) override {}
    OperatorResultType GetData(OperatorState& state, common::DataChunk& output,
                                ::cppcoldb::ClientContext& ctx) override;

    std::shared_ptr<SortBuffer> buf;
};

} // namespace cppcoldb::engine::execution
