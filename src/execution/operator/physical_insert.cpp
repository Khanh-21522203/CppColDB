#include "execution/operator/physical_insert.hpp"
#include "execution/pipeline.hpp"
#include "main/client_context.hpp"
#include "catalog/catalog.hpp"
#include "catalog/catalog_entry.hpp"
#include "storage/column/row_group.hpp"
#include "transaction/transaction.hpp"
#include "transaction/undo_buffer.hpp"
#include "common/exception.hpp"
#include <unordered_map>

namespace cppcoldb {

std::unique_ptr<OperatorState> PhysicalInsert::CreateScanState() const {
    return std::make_unique<ScanState>();
}

void PhysicalInsert::InitScan(OperatorState& /*state*/, ClientContext& /*ctx*/) {}

OperatorResultType PhysicalInsert::GetData(OperatorState& raw_state,
                                            DataChunk& /*output*/,
                                            ClientContext& ctx) {
    auto& state = static_cast<ScanState&>(raw_state);
    if (state.row_offset > 0) return OperatorResultType::FINISHED; // already executed
    ++state.row_offset;

    if (!ctx.catalog || !ctx.transaction) {
        throw RuntimeError("ClientContext missing catalog/transaction for INSERT");
    }

    if (rows.count == 0) return OperatorResultType::FINISHED;

    auto* table = ctx.catalog->GetTable(schema_name, table_name, *ctx.transaction);
    if (!table) {
        throw RuntimeError("INSERT: table not found: " + table_name);
    }

    const auto& pi = table->partition_info;

    if (!pi.IsPartitioned()) {
        // Non-partitioned: existing path unchanged.
        RowGroup* rg           = table->GetOrAddRowGroup();
        size_t    rg_id        = table->row_groups.size() - 1;
        size_t    append_start = rg->RowCount();

        rg->Append(rows, column_ids, ctx.transaction->tx_id);

        InsertUndoEntry ue;
        ue.schema        = schema_name;
        ue.table         = table_name;
        ue.row_group_id  = rg_id;
        ue.append_start  = append_start;
        ue.append_count  = rows.count;
        ue.inserted_rows = rows;
        ctx.transaction->undo_buffer.PushInsert(std::move(ue));
    } else {
        // Partitioned: group rows by partition, then append each group.
        // Find which chunk position holds the partition key column.
        int pk_chunk_idx = -1;
        for (size_t i = 0; i < column_ids.size(); ++i) {
            if (static_cast<int>(column_ids[i]) == pi.partition_col_idx) {
                pk_chunk_idx = static_cast<int>(i);
                break;
            }
        }
        if (pk_chunk_idx < 0)
            throw RuntimeError("INSERT: partition key column not included in INSERT");

        // Group row indices by partition id.
        std::unordered_map<uint32_t, std::vector<uint32_t>> by_partition;
        for (uint32_t r = 0; r < static_cast<uint32_t>(rows.count); ++r) {
            // Extract the key value from the partition column.
            const auto& pk_vec = rows.columns[pk_chunk_idx];
            Value key;
            if (!pk_vec.IsNull(r)) {
                TypeId t = pk_vec.type;
                if (t == TypeId::FLOAT32 || t == TypeId::FLOAT64) {
                    key = Value::Float(pk_vec.float_data[r]);
                    key.type = t;
                } else if (t == TypeId::VARCHAR) {
                    key = Value::Varchar(pk_vec.str_data[r]);
                } else {
                    key = Value::Integer(pk_vec.int_data[r]);
                    key.type = t;
                }
            }
            uint32_t pid = pi.RouteRow(key);
            by_partition[pid].push_back(r);
        }

        // Append per-partition slices.
        for (auto& [pid, row_indices] : by_partition) {
            DataChunk slice;
            DataChunkSlice(slice, rows, row_indices);

            RowGroup* rg           = table->GetOrAddRowGroupForPartition(pid);
            size_t    rg_id        = table->partition_rg_indices[pid].back();
            size_t    append_start = rg->RowCount();

            rg->Append(slice, column_ids, ctx.transaction->tx_id);

            InsertUndoEntry ue;
            ue.schema        = schema_name;
            ue.table         = table_name;
            ue.row_group_id  = rg_id;
            ue.append_start  = append_start;
            ue.append_count  = slice.count;
            ue.inserted_rows = slice;
            ctx.transaction->undo_buffer.PushInsert(std::move(ue));
        }
    }

    return OperatorResultType::FINISHED;
}

} // namespace cppcoldb
