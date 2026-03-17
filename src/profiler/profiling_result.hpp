#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace cppcoldb {

enum class QueryPhase {
    PARSE,
    BIND,
    OPTIMIZE,
    PHYSICAL_PLAN,
    EXECUTE,
};

inline const char* PhaseName(QueryPhase p) {
    switch (p) {
        case QueryPhase::PARSE:         return "PARSE";
        case QueryPhase::BIND:          return "BIND";
        case QueryPhase::OPTIMIZE:      return "OPTIMIZE";
        case QueryPhase::PHYSICAL_PLAN: return "PHYSICAL_PLAN";
        case QueryPhase::EXECUTE:       return "EXECUTE";
    }
    return "UNKNOWN";
}

struct PhaseProfile {
    QueryPhase  phase;
    std::string phase_name;
    int64_t     duration_us = 0; // microseconds
};

struct OperatorProfile {
    std::string operator_name;
    int64_t     total_time_ns = 0;
    int64_t     call_count    = 0;
    int64_t     rows_in       = 0;
    int64_t     rows_out      = 0;
};

struct ProfilingResult {
    std::string                   sql;
    int64_t                       total_duration_us = 0;
    std::vector<PhaseProfile>     phases;
    std::vector<OperatorProfile>  operators;

    std::string ToString() const;
};

} // namespace cppcoldb
