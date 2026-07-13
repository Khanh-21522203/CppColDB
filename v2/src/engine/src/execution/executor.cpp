#include "cppcoldb/engine/execution/executor.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::execution {

Executor::Executor(::cppcoldb::ClientContext& ctx) : ctx_(ctx) {}

void Executor::Initialize(std::unique_ptr<PhysicalOperator> plan) { CPPCOLDB_NOT_IMPLEMENTED(); }

void Executor::Execute() { CPPCOLDB_NOT_IMPLEMENTED(); }

QueryResult Executor::GetResult() { CPPCOLDB_NOT_IMPLEMENTED(); }

void Executor::BuildPipelines(PhysicalOperator* op, Pipeline* current_pipeline) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

std::vector<Pipeline*> Executor::TopologicalSort() { CPPCOLDB_NOT_IMPLEMENTED(); }

} // namespace cppcoldb::engine::execution
