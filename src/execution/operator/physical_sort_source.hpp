#pragma once
#include <memory>
#include "execution/physical_operator.hpp"
#include "execution/pipeline.hpp"
#include "execution/operator/physical_sort.hpp"

namespace cppcoldb {

struct SortSourceState : OperatorState {
    size_t chunk_idx = 0;
    size_t row_idx   = 0;
};

// SOURCE: emits sorted rows from SortBuffer one STANDARD_VECTOR_SIZE batch at a time.
struct PhysicalSortSource : PhysicalOperator {
    std::shared_ptr<SortBuffer> buf;

    PhysicalSortSource() { role = OperatorRole::SOURCE; }

    std::unique_ptr<OperatorState> CreateScanState() const override;
    void               InitScan(OperatorState& state, ClientContext& ctx) override {}
    OperatorResultType GetData (OperatorState& state, DataChunk& output,
                                ClientContext& ctx) override;
};

} // namespace cppcoldb
