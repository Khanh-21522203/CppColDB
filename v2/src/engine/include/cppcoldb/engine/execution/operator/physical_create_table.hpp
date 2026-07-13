#pragma once

#include <string>
#include <vector>

#include "cppcoldb/engine/catalog/catalog_entry.hpp"
#include "cppcoldb/engine/execution/physical_operator.hpp"
#include "cppcoldb/engine/storage/partition_info.hpp"

namespace cppcoldb::engine::execution {

// SOURCE: a single GetData() call registers the new table in the catalog
// (a no-op if `if_not_exists` and the table already exists); run-once via
// the generic ScanState cursor.
class PhysicalCreateTable : public PhysicalOperator {
public:
    PhysicalCreateTable() { role = OperatorRole::SOURCE; }

    std::unique_ptr<OperatorState> CreateScanState() const override;
    void InitScan(OperatorState& state, ::cppcoldb::ClientContext& ctx) override;
    OperatorResultType GetData(OperatorState& state, common::DataChunk& output,
                                ::cppcoldb::ClientContext& ctx) override;

    std::string                                  schema_name;
    std::string                                  table_name;
    std::vector<catalog::ColumnDefinition>       columns;
    bool                                          if_not_exists = false;
    storage::PartitionInfo                       partition_info;
};

} // namespace cppcoldb::engine::execution
