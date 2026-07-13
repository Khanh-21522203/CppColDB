#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cppcoldb/common/types/value.hpp"
#include "cppcoldb/engine/execution/physical_operator.hpp"

namespace cppcoldb::engine::execution {

// The kind of structural change an ALTER TABLE plan applies. This is
// execution's own, minimal vocabulary — the SQL binder/planner has its own
// higher-level ALTER TABLE representation, but execution must not depend on
// the sql module (see CONVENTIONS.md link-dependency table).
enum class AlterKind {
    ADD_COLUMN,
    DROP_COLUMN,
    RENAME_COLUMN,
    ADD_PARTITION,
    DROP_PARTITION,
    UPDATE_PARTITION_BOUND,
};

// SOURCE: a single GetData() call applies one structural change (`kind`) to
// the table's catalog entry and/or partition scheme; run-once via the
// generic ScanState cursor.
class PhysicalAlterTable : public PhysicalOperator {
public:
    PhysicalAlterTable() { role = OperatorRole::SOURCE; }

    std::unique_ptr<OperatorState> CreateScanState() const override;
    void InitScan(OperatorState& state, ::cppcoldb::ClientContext& ctx) override;
    OperatorResultType GetData(OperatorState& state, common::DataChunk& output,
                                ::cppcoldb::ClientContext& ctx) override;

    std::string                              schema_name;
    std::string                              table_name;
    AlterKind                                kind = AlterKind::ADD_COLUMN;
    std::uint32_t                            partition_idx = 0;
    std::vector<common::Value>               new_bound;
    std::vector<std::vector<common::Value>>  new_values;
};

} // namespace cppcoldb::engine::execution
