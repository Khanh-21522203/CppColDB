#pragma once
namespace cppcoldb {

enum class OperatorResultType {
    // Source: no more rows to produce; pipeline may terminate.
    // Operator: stop the entire pipeline immediately (e.g. LIMIT reached its cap).
    FINISHED,

    // Source: this chunk is full; call GetData() again for the next chunk.
    // Operator: produced output AND has more output from the SAME input chunk
    //           (e.g. HashJoinProbe expanding one probe row into many output rows).
    //           The executor MUST call Execute() again with the same input before
    //           fetching the next source chunk.
    HAVE_MORE_OUTPUT,

    // Operator only: done processing this input chunk (may or may not have
    // produced output); the executor should fetch the next source chunk.
    // Also used when an operator buffered input with no output yet
    // (e.g. hash join build side, aggregation accumulating state).
    NEED_MORE_INPUT,
};

} // namespace cppcoldb
