#pragma once

namespace cppcoldb::engine::execution {

// Result of one call into an operator's data-producing / flushing method.
enum class OperatorResultType {
    FINISHED,         // no more output will ever be produced by this operator
    HAVE_MORE_OUTPUT, // output is full but the operator still has buffered input to emit
    NEED_MORE_INPUT,  // output was not filled; caller should supply the next input chunk
};

} // namespace cppcoldb::engine::execution
