#include "execution/pipeline_executor.hpp"
#include "main/client_context.hpp"

namespace cppcoldb {

PipelineExecutor::PipelineExecutor(ClientContext& ctx, Pipeline& pipeline)
    : ctx_(ctx), pipeline_(pipeline) {}

void PipelineExecutor::Execute() {
    // --- Initialization ---
    source_state_ = pipeline_.source->CreateScanState();
    pipeline_.source->InitScan(*source_state_, ctx_);

    op_states_.resize(pipeline_.operators.size());
    for (size_t i = 0; i < pipeline_.operators.size(); ++i) {
        op_states_[i] = pipeline_.operators[i]->CreateOperatorState();
    }
    sink_state_ = pipeline_.sink->CreateSinkState();

    // --- Main loop ---
    while (true) {
        input_chunk_.Reset();
        auto source_result = pipeline_.source->GetData(
            *source_state_, input_chunk_, ctx_);

        if (source_result == OperatorResultType::FINISHED &&
            input_chunk_.count == 0) {
            TryFlushOperators();
            break;
        }

        // No intermediate operators: send directly to sink.
        if (pipeline_.operators.empty()) {
            if (input_chunk_.count > 0) {
                pipeline_.sink->Consume(input_chunk_, *sink_state_, ctx_);
            }
            if (source_result == OperatorResultType::FINISHED) {
                TryFlushOperators();
                break;
            }
            continue;
        }

        DataChunk* current_input = &input_chunk_;
        bool skip_to_next_source = false;

        for (size_t i = 0; i < pipeline_.operators.size() && !skip_to_next_source; ++i) {
            bool inner_done = false;

            while (!inner_done) {
                output_chunk_.Reset();
                auto op_result = pipeline_.operators[i]->Execute(
                    *current_input, output_chunk_, *op_states_[i], ctx_);

                if (op_result == OperatorResultType::FINISHED) {
                    // e.g. PhysicalLimit hit its row cap — flush last output then stop.
                    if (output_chunk_.count > 0) {
                        pipeline_.sink->Consume(output_chunk_, *sink_state_, ctx_);
                    }
                    pipeline_.sink->Finalize(*sink_state_, ctx_);
                    return;
                }

                if (op_result == OperatorResultType::NEED_MORE_INPUT) {
                    // Operator is done with this input chunk (output may or may not
                    // have been produced). Fetch the next source chunk.
                    if (output_chunk_.count > 0) {
                        pipeline_.sink->Consume(output_chunk_, *sink_state_, ctx_);
                    }
                    skip_to_next_source = true;
                    inner_done = true;
                    break;
                }

                // HAVE_MORE_OUTPUT: operator produced output and may produce more
                // from the SAME input chunk (e.g. HashJoinProbe with multi-row matches).
                if (i + 1 < pipeline_.operators.size()) {
                    // Pass output to the next operator.
                    // NOTE (Phase 8): if this middle operator returns HAVE_MORE_OUTPUT
                    // it must be re-called after the downstream chain completes.
                    // For Phase 5 operators this doesn't arise.
                    current_input = &output_chunk_;
                    inner_done = true; // advance outer for-loop to next operator
                } else {
                    // Last operator before sink.
                    if (output_chunk_.count > 0) {
                        pipeline_.sink->Consume(output_chunk_, *sink_state_, ctx_);
                    }
                    // HAVE_MORE_OUTPUT: call this operator again with the same input.
                    // inner_done stays false → inner loop continues.
                }
            }
        }

        // Source returned FINISHED with a non-empty last chunk; we've processed it.
        if (source_result == OperatorResultType::FINISHED) {
            TryFlushOperators();
            break;
        }
    }

    pipeline_.sink->Finalize(*sink_state_, ctx_);
}

void PipelineExecutor::TryFlushOperators() {
    for (size_t i = 0; i < pipeline_.operators.size(); ++i) {
        while (true) {
            output_chunk_.Reset();
            auto flush_result = pipeline_.operators[i]->TryFlush(
                output_chunk_, *op_states_[i], ctx_);

            if (output_chunk_.count > 0) {
                // Pass flushed output through downstream operators.
                DataChunk* current = &output_chunk_;
                DataChunk  next_out;
                bool pipeline_done = false;
                for (size_t j = i + 1; j < pipeline_.operators.size(); ++j) {
                    next_out.Reset();
                    auto res = pipeline_.operators[j]->Execute(
                        *current, next_out, *op_states_[j], ctx_);
                    current = &next_out;
                    if (res == OperatorResultType::FINISHED) {
                        pipeline_done = true;
                        break;
                    }
                }
                if (pipeline_done) return;
                if (current->count > 0) {
                    pipeline_.sink->Consume(*current, *sink_state_, ctx_);
                }
            }

            if (flush_result == OperatorResultType::FINISHED) break;
            // HAVE_MORE_OUTPUT: operator has more buffered output; flush again.
        }
    }
}

} // namespace cppcoldb
