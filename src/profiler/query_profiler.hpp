#pragma once
#include <chrono>
#include "profiler/profiling_result.hpp"

namespace cppcoldb {

class QueryProfiler {
public:
    QueryProfiler() = default;

    // Call at the start of a query (sets active_ = true).
    void StartQuery(const std::string& sql);

    // Bracket each named phase.
    void StartPhase(QueryPhase phase);
    void EndPhase(QueryPhase phase);

    // Finalize and return result (sets active_ = false).
    ProfilingResult EndQuery();

    // Register an operator; returns its index for RecordOperatorCall.
    size_t RegisterOperator(const std::string& name);

    // Accumulate one operator call's stats.
    void RecordOperatorCall(size_t idx, int64_t duration_ns,
                            int64_t rows_in, int64_t rows_out);

    bool IsActive() const { return active_; }

private:
    bool   active_ = false;

    std::string sql_;
    std::chrono::steady_clock::time_point query_start_;

    struct PhaseTimer {
        QueryPhase phase = QueryPhase::PARSE;
        std::chrono::steady_clock::time_point start;
    };
    PhaseTimer current_phase_;

    std::vector<PhaseProfile>    phase_profiles_;
    std::vector<OperatorProfile> operator_profiles_;
};

} // namespace cppcoldb
