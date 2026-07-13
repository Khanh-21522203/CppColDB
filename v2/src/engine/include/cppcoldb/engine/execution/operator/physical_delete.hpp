#pragma once

#include <memory>
#include <string>

#include "cppcoldb/engine/execution/operator/expr_evaluator.hpp"
#include "cppcoldb/engine/execution/physical_operator.hpp"

namespace cppcoldb::engine::execution {

// SOURCE: a single GetData() call scans the table and marks every row
// matching `predicate` as deleted; run-once via the generic ScanState cursor.
class PhysicalDelete : public PhysicalOperator {
public:
    PhysicalDelete() { role = OperatorRole::SOURCE; }

    std::unique_ptr<OperatorState> CreateScanState() const override;
    void InitScan(OperatorState& state, ::cppcoldb::ClientContext& ctx) override;
    OperatorResultType GetData(OperatorState& state, common::DataChunk& output,
                                ::cppcoldb::ClientContext& ctx) override;

    std::string                  schema_name;
    std::string                  table_name;
    std::unique_ptr<Expression>  predicate;
};

} // namespace cppcoldb::engine::execution
