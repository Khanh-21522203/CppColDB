#include "profiler/query_profiler.hpp"
#include <sstream>
#include <iomanip>
#include <cassert>

namespace cppcoldb {

// ---------------------------------------------------------------------------
// QueryProfiler
// ---------------------------------------------------------------------------

void QueryProfiler::StartQuery(const std::string& sql) {
    active_            = true;
    sql_               = sql;
    query_start_       = std::chrono::steady_clock::now();
    phase_profiles_.clear();
    operator_profiles_.clear();
}

void QueryProfiler::StartPhase(QueryPhase phase) {
    if (!active_) return;
    current_phase_ = {phase, std::chrono::steady_clock::now()};
}

void QueryProfiler::EndPhase(QueryPhase phase) {
    if (!active_) return;
    auto end = std::chrono::steady_clock::now();
    int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
                     end - current_phase_.start).count();
    phase_profiles_.push_back({phase, PhaseName(phase), us});
}

size_t QueryProfiler::RegisterOperator(const std::string& name) {
    size_t idx = operator_profiles_.size();
    operator_profiles_.push_back({name, 0, 0, 0, 0});
    return idx;
}

void QueryProfiler::RecordOperatorCall(size_t idx, int64_t duration_ns,
                                        int64_t rows_in, int64_t rows_out) {
    if (!active_) return;
    assert(idx < operator_profiles_.size());
    auto& p = operator_profiles_[idx];
    p.total_time_ns += duration_ns;
    p.call_count    += 1;
    p.rows_in       += rows_in;
    p.rows_out      += rows_out;
}

ProfilingResult QueryProfiler::EndQuery() {
    auto end = std::chrono::steady_clock::now();
    int64_t total_us = std::chrono::duration_cast<std::chrono::microseconds>(
                           end - query_start_).count();

    ProfilingResult r;
    r.sql              = sql_;
    r.total_duration_us = total_us;
    r.phases           = std::move(phase_profiles_);
    r.operators        = std::move(operator_profiles_);

    active_ = false;
    phase_profiles_.clear();
    operator_profiles_.clear();

    return r;
}

// ---------------------------------------------------------------------------
// ProfilingResult::ToString
// ---------------------------------------------------------------------------

std::string ProfilingResult::ToString() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);

    oss << "Query: " << sql << "\n";
    oss << "Total: " << (total_duration_us / 1000.0) << " ms\n\n";

    oss << "Phases:\n";
    for (const auto& ph : phases) {
        oss << "  " << ph.phase_name << ": "
            << (ph.duration_us / 1000.0) << " ms\n";
    }

    if (!operators.empty()) {
        int64_t total_ns = 0;
        for (const auto& op : operators) total_ns += op.total_time_ns;

        oss << "\nOperators:\n";
        for (const auto& op : operators) {
            double pct = (total_ns > 0)
                ? (100.0 * op.total_time_ns / total_ns)
                : 0.0;
            oss << "  " << op.operator_name
                << "  time=" << (op.total_time_ns / 1e6) << "ms"
                << "  rows_in=" << op.rows_in
                << "  rows_out=" << op.rows_out
                << "  calls=" << op.call_count
                << "  (" << pct << "%)\n";
        }
    }

    return oss.str();
}

} // namespace cppcoldb
