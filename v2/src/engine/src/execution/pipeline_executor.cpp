#include "cppcoldb/engine/execution/pipeline_executor.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::execution {

PipelineExecutor::PipelineExecutor(::cppcoldb::ClientContext& ctx, Pipeline& pipeline)
    : ctx_(ctx), pipeline_(pipeline) {}

void PipelineExecutor::Execute() { CPPCOLDB_NOT_IMPLEMENTED(); }

void PipelineExecutor::TryFlushOperators() { CPPCOLDB_NOT_IMPLEMENTED(); }

} // namespace cppcoldb::engine::execution
