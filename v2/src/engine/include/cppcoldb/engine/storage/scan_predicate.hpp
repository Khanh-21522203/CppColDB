#pragma once
#include <cstddef>

#include "cppcoldb/common/types/value.hpp"

namespace cppcoldb::engine::storage {

// Simple comparison operator for segment zone-map pruning.
enum class ScanPredicateOp { EQ, LT, LE, GT, GE };

// A single pushed-down predicate of the form: column[col_idx] op bound.
// col_idx is the remapped chunk-position index (0-based within the scan output).
struct ScanPredicate {
    std::size_t     col_idx;
    ScanPredicateOp op;
    common::Value   bound;
};

} // namespace cppcoldb::engine::storage
