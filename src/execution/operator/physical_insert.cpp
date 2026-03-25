#include "execution/operator/physical_insert.hpp"
#include "execution/pipeline.hpp"
#include "main/client_context.hpp"
#include "catalog/catalog.hpp"
#include "catalog/catalog_entry.hpp"
#include "storage/column/row_group.hpp"
#include "transaction/transaction.hpp"
#include "transaction/undo_buffer.hpp"
#include "common/exception.hpp"
#include <algorithm>
#include <unordered_map>

namespace cppcoldb {

// Compare two rows by a single typed column (for sort-before-append).
static bool CompareRowsByKey(const DataChunk& chunk, int col_idx, uint32_t a, uint32_t b) {
    const auto& v = chunk.columns[col_idx];
    TypeId t = v.type;
    if (t == TypeId::FLOAT32 || t == TypeId::FLOAT64)
        return v.float_data[a] < v.float_data[b];
    if (t == TypeId::VARCHAR)
        return v.str_data[a] < v.str_data[b];
    return v.int_data[a] < v.int_data[b];
}

// Core insert logic: insert a DataChunk into the target table (partitioned or not).
static void DoInsertChunk(const DataChunk& chunk,
                          const std::vector<size_t>& column_ids,
                          const std::string& schema_name,
                          const std::string& table_name,
                          TableCatalogEntry* table,
                          ClientContext& ctx) {
    const auto& pi = table->partition_info;

    if (!pi.IsPartitioned()) {
        RowGroup* rg           = table->GetOrAddRowGroup();
        size_t    rg_id        = table->row_groups.size() - 1;
        size_t    append_start = rg->RowCount();

        rg->Append(chunk, column_ids, ctx.transaction->tx_id);

        InsertUndoEntry ue;
        ue.schema        = schema_name;
        ue.table         = table_name;
        ue.row_group_id  = rg_id;
        ue.append_start  = append_start;
        ue.append_count  = chunk.count;
        ue.inserted_rows = chunk;
        ctx.transaction->undo_buffer.PushInsert(std::move(ue));
    } else {
        // Resolve chunk positions for each partition key column.
        const size_t nkeys = pi.partition_col_idxs.size();
        int pk_chunk_idxs[8] = {};  // stack-allocated; max 8 key columns
        size_t used_keys = std::min(nkeys, size_t(8));
        for (size_t k = 0; k < used_keys; ++k) {
            pk_chunk_idxs[k] = -1;
            for (size_t i = 0; i < column_ids.size(); ++i) {
                if (static_cast<int>(column_ids[i]) == pi.partition_col_idxs[k]) {
                    pk_chunk_idxs[k] = static_cast<int>(i);
                    break;
                }
            }
            if (pk_chunk_idxs[k] < 0)
                throw RuntimeError("INSERT: partition key column not included in INSERT");
        }

        std::unordered_map<uint32_t, std::vector<uint32_t>> by_partition;
        for (uint32_t r = 0; r < static_cast<uint32_t>(chunk.count); ++r) {
            // Build composite key on the stack — no heap allocation.
            Value keys[8];
            for (size_t k = 0; k < used_keys; ++k) {
                const auto& vec = chunk.columns[pk_chunk_idxs[k]];
                if (vec.IsNull(r))
                    throw RuntimeError("partition key must not be NULL");
                TypeId t = vec.type;
                if (t == TypeId::FLOAT32 || t == TypeId::FLOAT64) {
                    keys[k] = Value::Float(vec.float_data[r]); keys[k].type = t;
                } else if (t == TypeId::VARCHAR) {
                    keys[k] = Value::Varchar(vec.str_data[r]);
                } else {
                    keys[k] = Value::Integer(vec.int_data[r]); keys[k].type = t;
                }
            }
            uint32_t pid = pi.RouteRow(keys, used_keys);
            by_partition[pid].push_back(r);
        }

        for (auto& [pid, row_indices] : by_partition) {
            // Sort by leading partition key column (for zone-map effectiveness).
            const int sort_col = pk_chunk_idxs[0];
            std::sort(row_indices.begin(), row_indices.end(),
                [&](uint32_t a, uint32_t b) {
                    return CompareRowsByKey(chunk, sort_col, a, b);
                });
            DataChunk slice;
            DataChunkSlice(slice, chunk, row_indices);

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
}

std::unique_ptr<OperatorState> PhysicalInsert::CreateScanState() const {
    return std::make_unique<ScanState>();
}

std::unique_ptr<OperatorState> PhysicalInsert::CreateSinkState() const {
    return std::make_unique<ScanState>();
}

void PhysicalInsert::InitScan(OperatorState& /*state*/, ClientContext& /*ctx*/) {}

OperatorResultType PhysicalInsert::GetData(OperatorState& raw_state,
                                            DataChunk& /*output*/,
                                            ClientContext& ctx) {
    auto& state = static_cast<ScanState&>(raw_state);
    if (state.row_offset > 0) return OperatorResultType::FINISHED;
    ++state.row_offset;

    if (!ctx.catalog || !ctx.transaction)
        throw RuntimeError("ClientContext missing catalog/transaction for INSERT");
    if (rows.count == 0) return OperatorResultType::FINISHED;

    auto* table = ctx.catalog->GetTable(schema_name, table_name, *ctx.transaction);
    if (!table) throw RuntimeError("INSERT: table not found: " + table_name);

    DoInsertChunk(rows, column_ids, schema_name, table_name, table, ctx);
    return OperatorResultType::FINISHED;
}

// SINK path for INSERT...SELECT: called once per chunk from SELECT.
void PhysicalInsert::Consume(const DataChunk& input, OperatorState& /*state*/,
                              ClientContext& ctx) {
    if (!ctx.catalog || !ctx.transaction)
        throw RuntimeError("ClientContext missing catalog/transaction for INSERT...SELECT");
    if (input.count == 0) return;

    auto* table = ctx.catalog->GetTable(schema_name, table_name, *ctx.transaction);
    if (!table) throw RuntimeError("INSERT: table not found: " + table_name);

    DoInsertChunk(input, column_ids, schema_name, table_name, table, ctx);
}

} // namespace cppcoldb
