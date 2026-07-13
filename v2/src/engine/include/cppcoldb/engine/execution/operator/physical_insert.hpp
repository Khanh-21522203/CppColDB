#pragma once

#include <string>
#include <vector>

#include "cppcoldb/common/types/column_id.hpp"
#include "cppcoldb/common/types/data_chunk.hpp"
#include "cppcoldb/engine/execution/physical_operator.hpp"

namespace cppcoldb::engine::execution {

// Dual-role: SOURCE for `INSERT ... VALUES` (GetData() appends the literal
// `rows` chunk to the table once, using the generic ScanState cursor to make
// itself run-once), SINK for `INSERT ... SELECT` (Consume() appends each
// chunk produced by the child SELECT plan as it arrives).
class PhysicalInsert : public PhysicalOperator {
public:
    PhysicalInsert() { role = OperatorRole::SOURCE; }

    std::unique_ptr<OperatorState> CreateScanState() const override;
    std::unique_ptr<OperatorState> CreateSinkState() const override;
    void InitScan(OperatorState& state, ::cppcoldb::ClientContext& ctx) override;
    OperatorResultType GetData(OperatorState& state, common::DataChunk& output,
                                ::cppcoldb::ClientContext& ctx) override;
    void Consume(const common::DataChunk& input, OperatorState& state,
                 ::cppcoldb::ClientContext& ctx) override;

    std::string                   schema_name;
    std::string                   table_name;
    std::vector<common::ColumnId> column_ids;
    common::DataChunk             rows;
    bool                          has_select_source = false;
};

} // namespace cppcoldb::engine::execution
