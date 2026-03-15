#include "execution/operator/physical_table_scan.hpp"
#include "main/client_context.hpp"
#include "catalog/catalog.hpp"
#include "catalog/catalog_entry.hpp"
#include "transaction/transaction.hpp"
#include "storage/column/row_group.hpp"
#include "common/exception.hpp"

namespace cppcoldb {

std::unique_ptr<OperatorState> PhysicalTableScan::CreateScanState() const {
    return std::make_unique<TableScanState>();
}

void PhysicalTableScan::InitScan(OperatorState& /*state*/, ClientContext& /*ctx*/) {
    // no-op: TableScanState default-initializes to row_group_idx=0, row_offset_in_group=0
}

OperatorResultType PhysicalTableScan::GetData(OperatorState& raw_state,
                                               DataChunk& output,
                                               ClientContext& ctx) {
    auto& state = static_cast<TableScanState&>(raw_state);

    if (!ctx.catalog || !ctx.transaction) {
        return OperatorResultType::FINISHED;
    }

    TableCatalogEntry* table = ctx.catalog->GetTable(schema_name, table_name, *ctx.transaction);
    if (!table) return OperatorResultType::FINISHED;

    auto& row_groups = table->row_groups;

    while (state.row_group_idx < row_groups.size()) {
        RowGroup* rg = row_groups[state.row_group_idx].get();

        size_t rows_read = rg->Scan(state.row_offset_in_group, column_ids, output, *ctx.transaction);

        if (state.row_offset_in_group >= rg->RowCount()) {
            ++state.row_group_idx;
            state.row_offset_in_group = 0;
        }

        if (output.count > 0) {
            return OperatorResultType::HAVE_MORE_OUTPUT;
        }
        // Empty chunk from this row group; try next
    }

    return OperatorResultType::FINISHED;
}

} // namespace cppcoldb
