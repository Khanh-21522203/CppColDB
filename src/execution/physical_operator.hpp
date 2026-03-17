#pragma once
#include <vector>
#include <memory>
#include <string>
#include "common/types.hpp"
#include "execution/operator_result_type.hpp"

namespace cppcoldb {

struct ClientContext;
struct OperatorState;
struct ScanState;

enum class OperatorRole { SOURCE, OPERATOR, SINK };

// Base class for all physical query operators.
// Subclasses implement one or more of the SOURCE / OPERATOR / SINK roles.
struct PhysicalOperator {
    OperatorRole             role = OperatorRole::OPERATOR;
    std::vector<TypeId>      output_types;
    std::vector<std::string> output_names;
    std::vector<std::unique_ptr<PhysicalOperator>> children;
    int                      profile_idx = -1; // -1 = unregistered

    virtual ~PhysicalOperator() = default;

    // --- Factory methods for per-instance state ---
    virtual std::unique_ptr<OperatorState> CreateScanState()     const;
    virtual std::unique_ptr<OperatorState> CreateOperatorState() const;
    virtual std::unique_ptr<OperatorState> CreateSinkState()     const;

    // --- SOURCE role ---
    virtual void               InitScan(OperatorState& state, ClientContext& ctx) {}
    virtual OperatorResultType GetData (OperatorState& state, DataChunk& output,
                                        ClientContext& ctx) {
        return OperatorResultType::FINISHED;
    }

    // --- OPERATOR role ---
    // Execute: transform input → output.  Returns FINISHED / HAVE_MORE_OUTPUT / NEEDS_MORE_INPUT.
    virtual OperatorResultType Execute(const DataChunk& input, DataChunk& output,
                                       OperatorState& state, ClientContext& ctx) {
        return OperatorResultType::FINISHED;
    }

    // TryFlush: called after source exhausted; emit any buffered state (aggregation, sort).
    virtual OperatorResultType TryFlush(DataChunk& output, OperatorState& state,
                                        ClientContext& ctx) {
        return OperatorResultType::FINISHED;
    }

    // --- SINK role ---
    virtual void Consume (const DataChunk& input, OperatorState& state, ClientContext& ctx) {}
    virtual void Finalize(OperatorState& state, ClientContext& ctx) {}
};

} // namespace cppcoldb
