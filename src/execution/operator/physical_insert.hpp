#pragma once
#include "execution/physical_operator.hpp"

namespace cppcoldb {

struct PhysicalInsert : PhysicalOperator {
    std::string         schema_name;
    std::string         table_name;
    std::vector<size_t> column_ids; // target column indices in table order
    DataChunk           rows;       // pre-evaluated rows to insert

    PhysicalInsert() { role = OperatorRole::SOURCE; }

    std::unique_ptr<OperatorState> CreateScanState() const override;
    void               InitScan(OperatorState& state, ClientContext& ctx) override;
    OperatorResultType GetData (OperatorState& state, DataChunk& output,
                                ClientContext& ctx) override;
};

} // namespace cppcoldb
