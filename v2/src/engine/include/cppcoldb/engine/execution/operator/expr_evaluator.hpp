#pragma once

#include <cstdint>
#include <vector>

#include "cppcoldb/common/types/data_chunk.hpp"

namespace cppcoldb::engine::execution {

// Opaque handle for one bound/compiled scalar expression tree. The SQL
// binder (sql::planner) owns the real node hierarchy (column refs,
// literals, binary/unary ops, casts, aggregates); execution must not depend
// on the sql module (see CONVENTIONS.md link-dependency table), so it only
// ever sees expressions through this base type. The planner's concrete
// expression classes will derive from it once wired up.
class Expression {
public:
    virtual ~Expression() = default;
};

// Evaluates a scalar `expr` over every row of `chunk`, writing one value per
// row into `output`.
class ExprEvaluator {
public:
    static void Evaluate(const Expression& expr, const common::DataChunk& chunk,
                         common::DataVector& output);

    // Evaluates a boolean predicate over `chunk`; returns the (1-based? see
    // impl) row indices that satisfied it, for use with DataChunkCompact.
    static std::vector<std::uint16_t> EvaluatePredicate(const Expression& pred,
                                                          const common::DataChunk& chunk);
};

// Copies the rows of `src` named by `selection` into `dst`, compacting out
// any rows that did not survive a filter.
void DataChunkCompact(const common::DataChunk& src, const std::vector<std::uint16_t>& selection,
                      common::DataChunk& dst);

} // namespace cppcoldb::engine::execution
