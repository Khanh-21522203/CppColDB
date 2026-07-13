#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "cppcoldb/common/types/data_chunk.hpp"
#include "cppcoldb/engine/execution/operator/expr_evaluator.hpp"
#include "cppcoldb/engine/execution/physical_operator.hpp"

namespace cppcoldb::engine::execution {

// All chunks consumed so far, one key DataVector per sort key per chunk, and
// (once Finalize() has run) the row order the sort settled on. Shared with
// the paired PhysicalSortSource via a shared_ptr so the SINK and SOURCE
// halves of one ORDER BY can be split across two Pipelines.
struct SortBuffer {
    std::vector<common::DataChunk>                    chunks;
    std::vector<std::vector<common::DataVector>>       key_vecs;
    std::vector<std::pair<std::size_t, std::size_t>>   sorted_order;
    bool                                               sorted = false;
};

struct SortSinkState : public OperatorState {
    std::shared_ptr<SortBuffer> buf;
};

// SINK: buffers every input chunk (plus its evaluated sort keys) until
// Finalize(), at which point the full row order is computed and stashed in
// `buf` for PhysicalSortSource to stream back out.
class PhysicalSort : public PhysicalOperator {
public:
    PhysicalSort() { role = OperatorRole::SINK; }

    std::unique_ptr<OperatorState> CreateSinkState() const override;
    void Consume(const common::DataChunk& input, OperatorState& state,
                 ::cppcoldb::ClientContext& ctx) override;
    void Finalize(OperatorState& state, ::cppcoldb::ClientContext& ctx) override;

    std::vector<std::unique_ptr<Expression>> sort_keys;
    std::vector<bool>                        ascending;
    std::int64_t                             top_k = -1;
    std::shared_ptr<SortBuffer>               buf;
    std::unique_ptr<PhysicalOperator>         source_op;
};

} // namespace cppcoldb::engine::execution
