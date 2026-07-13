#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cppcoldb::engine::profiler {

// The distinct phases of one query's lifecycle, timed independently by QueryProfiler.
enum class QueryPhase { PARSE, BIND, OPTIMIZE, PHYSICAL_PLAN, EXECUTE };

// Human-readable name for a QueryPhase (e.g. "PARSE"); "UNKNOWN" for an
// out-of-range value.
const char* PhaseName(QueryPhase phase);

// Wall-clock time spent in one QueryPhase.
struct PhaseProfile {
    QueryPhase  phase = QueryPhase::PARSE;
    std::string phase_name;
    std::int64_t duration_us = 0;
};

// Aggregated call statistics for one registered physical operator instance.
struct OperatorProfile {
    std::string  operator_name;
    std::int64_t total_time_ns = 0;
    std::int64_t call_count = 0;
    std::int64_t rows_in = 0;
    std::int64_t rows_out = 0;
};

// Full profiling snapshot for one executed query, produced by QueryProfiler::EndQuery().
struct ProfilingResult {
    std::string                  sql;
    std::int64_t                 total_duration_us = 0;
    std::vector<PhaseProfile>    phases;
    std::vector<OperatorProfile> operators;

    std::string ToString() const;
};

} // namespace cppcoldb::engine::profiler
