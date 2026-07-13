#pragma once

#include <memory>
#include <vector>

#include "cppcoldb/common/types/data_chunk.hpp"
#include "cppcoldb/engine/execution/pipeline.hpp"

namespace cppcoldb {
class ClientContext;
} // namespace cppcoldb

namespace cppcoldb::engine::execution {

// Drives one Pipeline to completion: pulls chunks from the source, pushes
// them through the intermediate operators, and feeds the result into the
// sink, looping TryFlush() for operators that buffer more output than they
// were given input.
class PipelineExecutor {
public:
    PipelineExecutor(::cppcoldb::ClientContext& ctx, Pipeline& pipeline);

    void Execute();

private:
    void TryFlushOperators();

    ::cppcoldb::ClientContext& ctx_;
    Pipeline&                  pipeline_;
    common::DataChunk          input_chunk_;
    common::DataChunk          output_chunk_;

    std::vector<std::unique_ptr<OperatorState>> op_states_;
    std::unique_ptr<OperatorState>              source_state_;
    std::unique_ptr<OperatorState>              sink_state_;
};

} // namespace cppcoldb::engine::execution
