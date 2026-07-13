#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cppcoldb/engine/profiler/profiling_result.hpp"

namespace cppcoldb::engine::profiler {

// Accumulates phase timings and per-operator call statistics for one query
// execution, from StartQuery() through EndQuery().
class QueryProfiler {
public:
    QueryProfiler() = default;

    void StartQuery(const std::string& sql);
    void StartPhase(QueryPhase phase);
    void EndPhase(QueryPhase phase);
    ProfilingResult EndQuery();

    // Registers a physical operator instance for per-call tracking; returns
    // the index later passed to RecordOperatorCall() (and stashed on the
    // operator as PhysicalOperator::profile_idx).
    std::size_t RegisterOperator(const std::string& name);
    void RecordOperatorCall(std::size_t idx, std::int64_t duration_ns, std::int64_t rows_in,
                             std::int64_t rows_out);

    bool IsActive() const { return active_; }

private:
    struct PhaseTimer {
        QueryPhase                           phase = QueryPhase::PARSE;
        std::chrono::steady_clock::time_point start;
    };

    bool                                   active_ = false;
    std::string                            sql_;
    std::chrono::steady_clock::time_point  query_start_;
    PhaseTimer                             current_phase_;
    std::vector<PhaseProfile>              phase_profiles_;
    std::vector<OperatorProfile>           operator_profiles_;
};

} // namespace cppcoldb::engine::profiler
