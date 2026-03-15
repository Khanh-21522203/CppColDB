#pragma once
#include <vector>
#include <memory>
#include "execution/pipeline.hpp"
#include "common/types.hpp"

namespace cppcoldb {

struct ClientContext;

class PipelineExecutor {
public:
    PipelineExecutor(ClientContext& ctx, Pipeline& pipeline);

    // Run the pipeline to completion.
    void Execute();

private:
    // Flush any buffered operator state after source is exhausted.
    void TryFlushOperators();

    ClientContext& ctx_;
    Pipeline&      pipeline_;
    DataChunk      input_chunk_;
    DataChunk      output_chunk_;

    std::vector<std::unique_ptr<OperatorState>> op_states_;
    std::unique_ptr<OperatorState>              source_state_;
    std::unique_ptr<OperatorState>              sink_state_;
};

} // namespace cppcoldb
