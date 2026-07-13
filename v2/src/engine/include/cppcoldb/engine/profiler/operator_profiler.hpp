#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

#include "cppcoldb/engine/profiler/query_profiler.hpp"

namespace cppcoldb::engine::profiler {

// RAII call-timer: on construction records the start time and rows-in count;
// on destruction reports elapsed wall-clock time and rows-out to the owning
// QueryProfiler. Header-only, mirroring v1 — this is trivial timing
// plumbing, not engine logic, so it is fully implemented rather than stubbed.
class OperatorProfileGuard {
public:
    OperatorProfileGuard(QueryProfiler& profiler, std::size_t op_idx, std::int64_t rows_in)
        : profiler_(profiler), op_idx_(op_idx), rows_in_(rows_in),
          start_(std::chrono::steady_clock::now()) {}

    ~OperatorProfileGuard() {
        const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now() - start_)
                                     .count();
        profiler_.RecordOperatorCall(op_idx_, elapsed_ns, rows_in_, rows_out_);
    }

    OperatorProfileGuard(const OperatorProfileGuard&) = delete;
    OperatorProfileGuard& operator=(const OperatorProfileGuard&) = delete;

    void SetRowsOut(std::int64_t n) { rows_out_ = n; }

private:
    QueryProfiler&                         profiler_;
    std::size_t                            op_idx_;
    std::int64_t                           rows_in_;
    std::int64_t                           rows_out_ = 0;
    std::chrono::steady_clock::time_point  start_;
};

} // namespace cppcoldb::engine::profiler
