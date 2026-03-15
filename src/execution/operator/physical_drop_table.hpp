#pragma once
#include "execution/physical_operator.hpp"

namespace cppcoldb {

struct PhysicalDropTable : PhysicalOperator {
    std::string schema_name;
    std::string table_name;
    bool if_exists = false;

    PhysicalDropTable() { role = OperatorRole::SOURCE; }

    std::unique_ptr<OperatorState> CreateScanState() const override;
    void               InitScan(OperatorState& state, ClientContext& ctx) override;
    OperatorResultType GetData (OperatorState& state, DataChunk& output,
                                ClientContext& ctx) override;
};

} // namespace cppcoldb
