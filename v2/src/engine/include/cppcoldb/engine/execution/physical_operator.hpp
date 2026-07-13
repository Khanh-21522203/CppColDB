#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cppcoldb/common/types/type_id.hpp"
#include "cppcoldb/engine/abstractions/execution/i_physical_operator.hpp"

namespace cppcoldb::engine::execution {

// The role an operator plays within a Pipeline; determines which subset of
// IPhysicalOperator's methods are meaningfully driven by the executor.
enum class OperatorRole { SOURCE, OPERATOR, SINK };

// Base class for all per-execution operator scratch state. One instance is
// created per PipelineExecutor run via the owning operator's CreateXState().
struct OperatorState {
    virtual ~OperatorState() = default;
};

// Generic row-group/offset scan cursor, reused by operators whose GetData()
// is a simple single-pass, run-once cursor (see operator/ subdirectory).
struct ScanState : public OperatorState {
    std::size_t current_row_group = 0;
    std::size_t row_offset = 0;
};

// Concrete base for every node of a physical query plan. Provides the
// default (no-op / FINISHED) behavior for the two roles an operator does NOT
// play, so a derived class only has to override the methods for its own role.
class PhysicalOperator : public IPhysicalOperator {
public:
    PhysicalOperator() = default;
    ~PhysicalOperator() override = default;

    std::unique_ptr<OperatorState> CreateScanState() const override;
    std::unique_ptr<OperatorState> CreateOperatorState() const override;
    std::unique_ptr<OperatorState> CreateSinkState() const override;

    void InitScan(OperatorState& state, ::cppcoldb::ClientContext& ctx) override {}
    OperatorResultType GetData(OperatorState& state, common::DataChunk& output,
                                ::cppcoldb::ClientContext& ctx) override {
        return OperatorResultType::FINISHED;
    }

    OperatorResultType Execute(const common::DataChunk& input, common::DataChunk& output,
                                OperatorState& state, ::cppcoldb::ClientContext& ctx) override {
        return OperatorResultType::FINISHED;
    }
    OperatorResultType TryFlush(common::DataChunk& output, OperatorState& state,
                                 ::cppcoldb::ClientContext& ctx) override {
        return OperatorResultType::FINISHED;
    }

    void Consume(const common::DataChunk& input, OperatorState& state,
                 ::cppcoldb::ClientContext& ctx) override {}
    void Finalize(OperatorState& state, ::cppcoldb::ClientContext& ctx) override {}

    OperatorRole                                   role = OperatorRole::OPERATOR;
    std::vector<common::TypeId>                    output_types;
    std::vector<std::string>                       output_names;
    std::vector<std::unique_ptr<PhysicalOperator>> children;
    int                                             profile_idx = -1; // -1 = unregistered
};

} // namespace cppcoldb::engine::execution
