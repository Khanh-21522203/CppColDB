#pragma once

#include <memory>

#include "cppcoldb/common/types/data_chunk.hpp"
#include "cppcoldb/engine/execution/operator_result_type.hpp"

namespace cppcoldb {
class ClientContext;
} // namespace cppcoldb

namespace cppcoldb::engine::execution {

// Per-execution scratch state threaded through one role's methods for the
// lifetime of a single PipelineExecutor run. Concrete hierarchy lives in
// physical_operator.hpp (ScanState) and alongside each concrete operator
// (e.g. TableScanState, JoinProbeState, SortSinkState, HashAggState).
struct OperatorState;

// Pure-virtual contract implemented by every node in a physical query plan.
// A node plays exactly one of three roles (OperatorRole, see physical_operator.hpp):
//   - SOURCE:   drives GetData() to produce chunks; InitScan() prepares its state.
//   - OPERATOR: transforms an input chunk into an output chunk via Execute()/TryFlush().
//   - SINK:     accumulates input via Consume() and produces its result in Finalize().
class IPhysicalOperator {
public:
    virtual ~IPhysicalOperator() = default;

    // Per-instance state factories; the returned OperatorState is owned by the
    // caller (typically a PipelineExecutor) and passed back into every call
    // below for the operator's role.
    virtual std::unique_ptr<OperatorState> CreateScanState() const = 0;
    virtual std::unique_ptr<OperatorState> CreateOperatorState() const = 0;
    virtual std::unique_ptr<OperatorState> CreateSinkState() const = 0;

    // SOURCE role.
    virtual void InitScan(OperatorState& state, ::cppcoldb::ClientContext& ctx) = 0;
    virtual OperatorResultType GetData(OperatorState& state, common::DataChunk& output,
                                        ::cppcoldb::ClientContext& ctx) = 0;

    // OPERATOR role.
    virtual OperatorResultType Execute(const common::DataChunk& input, common::DataChunk& output,
                                        OperatorState& state, ::cppcoldb::ClientContext& ctx) = 0;
    virtual OperatorResultType TryFlush(common::DataChunk& output, OperatorState& state,
                                        ::cppcoldb::ClientContext& ctx) = 0;

    // SINK role.
    virtual void Consume(const common::DataChunk& input, OperatorState& state,
                         ::cppcoldb::ClientContext& ctx) = 0;
    virtual void Finalize(OperatorState& state, ::cppcoldb::ClientContext& ctx) = 0;
};

} // namespace cppcoldb::engine::execution
