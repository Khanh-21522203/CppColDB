#pragma once

#include <string>

#include "cppcoldb/engine/execution/physical_operator.hpp"

namespace cppcoldb::engine::execution {

// SOURCE: a single GetData() call marks the table dropped in the catalog
// (a no-op if `if_exists` and the table does not exist); run-once via the
// generic ScanState cursor.
class PhysicalDropTable : public PhysicalOperator {
public:
    PhysicalDropTable() { role = OperatorRole::SOURCE; }

    std::unique_ptr<OperatorState> CreateScanState() const override;
    void InitScan(OperatorState& state, ::cppcoldb::ClientContext& ctx) override;
    OperatorResultType GetData(OperatorState& state, common::DataChunk& output,
                                ::cppcoldb::ClientContext& ctx) override;

    std::string schema_name;
    std::string table_name;
    bool        if_exists = false;
};

} // namespace cppcoldb::engine::execution
