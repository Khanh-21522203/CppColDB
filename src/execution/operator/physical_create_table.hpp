#pragma once
#include "execution/physical_operator.hpp"
#include "catalog/catalog_entry.hpp"

namespace cppcoldb {

struct PhysicalCreateTable : PhysicalOperator {
    std::string schema_name;
    std::string table_name;
    std::vector<ColumnDefinition> columns;
    bool          if_not_exists  = false;
    PartitionInfo partition_info;

    PhysicalCreateTable() { role = OperatorRole::SOURCE; }

    std::unique_ptr<OperatorState> CreateScanState() const override;
    void               InitScan(OperatorState& state, ClientContext& ctx) override;
    OperatorResultType GetData (OperatorState& state, DataChunk& output,
                                ClientContext& ctx) override;
};

} // namespace cppcoldb
