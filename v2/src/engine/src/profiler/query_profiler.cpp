#include "cppcoldb/engine/profiler/query_profiler.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::profiler {

void QueryProfiler::StartQuery(const std::string& sql) { CPPCOLDB_NOT_IMPLEMENTED(); }

void QueryProfiler::StartPhase(QueryPhase phase) { CPPCOLDB_NOT_IMPLEMENTED(); }

void QueryProfiler::EndPhase(QueryPhase phase) { CPPCOLDB_NOT_IMPLEMENTED(); }

ProfilingResult QueryProfiler::EndQuery() { CPPCOLDB_NOT_IMPLEMENTED(); }

std::size_t QueryProfiler::RegisterOperator(const std::string& name) { CPPCOLDB_NOT_IMPLEMENTED(); }

void QueryProfiler::RecordOperatorCall(std::size_t idx, std::int64_t duration_ns, std::int64_t rows_in,
                                        std::int64_t rows_out) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::profiler
