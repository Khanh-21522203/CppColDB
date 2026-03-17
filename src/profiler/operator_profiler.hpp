#pragma once
#include <chrono>
#include "profiler/query_profiler.hpp"

namespace cppcoldb {

// RAII guard that times a single operator call.
// Construct before the call, set rows_out after; destructor records stats.
class OperatorProfileGuard {
public:
    OperatorProfileGuard(QueryProfiler& profiler, size_t op_idx, int64_t rows_in)
        : profiler_(profiler),
          op_idx_(op_idx),
          rows_in_(rows_in),
          start_(std::chrono::steady_clock::now()) {}

    ~OperatorProfileGuard() {
        auto end = std::chrono::steady_clock::now();
        int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         end - start_).count();
        profiler_.RecordOperatorCall(op_idx_, ns, rows_in_, rows_out_);
    }

    void SetRowsOut(int64_t n) { rows_out_ = n; }

    // Non-copyable, non-movable.
    OperatorProfileGuard(const OperatorProfileGuard&) = delete;
    OperatorProfileGuard& operator=(const OperatorProfileGuard&) = delete;

private:
    QueryProfiler&                        profiler_;
    size_t                                op_idx_;
    int64_t                               rows_in_;
    int64_t                               rows_out_ = 0;
    std::chrono::steady_clock::time_point start_;
};

} // namespace cppcoldb
