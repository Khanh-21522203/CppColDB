#pragma once
#include "execution/physical_operator.hpp"

namespace cppcoldb {

struct PhysicalInsert : PhysicalOperator {
    std::string         schema_name;
    std::string         table_name;
    std::vector<size_t> column_ids;       // target column indices in table order
    DataChunk           rows;             // pre-evaluated rows (VALUES path)
    bool                has_select_source = false; // children[0] = scan pipeline

    PhysicalInsert() { role = OperatorRole::SOURCE; }

    std::unique_ptr<OperatorState> CreateScanState() const override;
    std::unique_ptr<OperatorState> CreateSinkState() const override;
    void               InitScan (OperatorState& state, ClientContext& ctx) override;
    OperatorResultType GetData  (OperatorState& state, DataChunk& output,
                                 ClientContext& ctx) override;
    void               Consume  (const DataChunk& input, OperatorState& state,
                                 ClientContext& ctx) override;
};

} // namespace cppcoldb
