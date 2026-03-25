#include "main/database.hpp"
#include "main/connection.hpp"
#include "storage/buffer_manager.hpp"
#include "storage/block_file.hpp"
#include "storage/wal.hpp"
#include "catalog/catalog.hpp"
#include "catalog/catalog_entry.hpp"
#include "transaction/transaction_manager.hpp"
#include "transaction/transaction.hpp"
#include "transaction/undo_buffer.hpp"
#include "checkpoint/checkpoint_manager.hpp"
#include "parallel/task_scheduler.hpp"
#include "storage/column/row_group.hpp"
#include "common/types.hpp"
#include "common/exception.hpp"
#include <sys/stat.h>
#include <cstring>
#include <algorithm>
#include <numeric>
#include <unordered_map>

namespace cppcoldb {

// ---------------------------------------------------------------------------
// WAL binary-format read helpers (little-endian)
// ---------------------------------------------------------------------------
static uint8_t  RdU8 (const uint8_t*& p) { return *p++; }
static uint16_t RdU16(const uint8_t*& p) { uint16_t v=(uint16_t)*p++;v|=(uint16_t)(*p++)<<8;return v; }
static uint32_t RdU32(const uint8_t*& p) { uint32_t v=0;for(int i=0;i<4;i++)v|=((uint32_t)*p++)<<(8*i);return v; }
static uint64_t RdU64(const uint8_t*& p) { uint64_t v=0;for(int i=0;i<8;i++)v|=((uint64_t)*p++)<<(8*i);return v; }
static int64_t  RdI64(const uint8_t*& p) { return (int64_t)RdU64(p); }
static double   RdF64(const uint8_t*& p) { uint64_t bits=RdU64(p);double v;memcpy(&v,&bits,8);return v; }
static std::string RdStr(const uint8_t*& p) { uint16_t len=RdU16(p);std::string s(reinterpret_cast<const char*>(p),len);p+=len;return s; }

// ---------------------------------------------------------------------------
// Decode a DataChunk payload produced by WAL::SerializeChunk.
// ---------------------------------------------------------------------------
static DataChunk DeserializeChunkPayload(const uint8_t*& p) {
    uint32_t row_count = RdU32(p);
    uint32_t col_count = RdU32(p);

    std::vector<TypeId> types;
    types.reserve(col_count);
    for (uint32_t c = 0; c < col_count; ++c) {
        types.push_back(static_cast<TypeId>(RdU8(p)));
    }

    DataChunk chunk;
    chunk.Initialize(types);
    chunk.count = row_count;

    for (uint32_t c = 0; c < col_count; ++c) {
        DataVector& vec = chunk.columns[c];
        vec.count = row_count;
        TypeId t  = types[c];

        // Read validity bitmap: ceil(row_count / 8) bytes, bit i = row i is NOT NULL
        size_t bitmap_bytes = (row_count + 7) / 8;
        std::vector<uint8_t> bitmap(bitmap_bytes);
        for (size_t b = 0; b < bitmap_bytes; ++b) bitmap[b] = RdU8(p);

        // Decode non-null values in row order
        for (uint32_t r = 0; r < row_count; ++r) {
            bool not_null = (bitmap[r / 8] >> (r % 8)) & 1;
            if (!not_null) {
                vec.SetNull(r);
                continue;
            }
            vec.validity.set(r);
            switch (t) {
                case TypeId::BOOLEAN:
                case TypeId::INT8:
                case TypeId::INT16:
                case TypeId::INT32:
                case TypeId::INT64:
                    vec.int_data[r] = RdI64(p);
                    break;
                case TypeId::FLOAT32:
                case TypeId::FLOAT64:
                    vec.float_data[r] = RdF64(p);
                    break;
                case TypeId::VARCHAR: {
                    uint16_t slen = RdU16(p);
                    vec.str_data[r] = std::string(reinterpret_cast<const char*>(p), slen);
                    p += slen;
                    break;
                }
                default:
                    break;
            }
        }
    }

    return chunk;
}

// ---------------------------------------------------------------------------
// Database
// ---------------------------------------------------------------------------
Database::Database(const std::string& path, DatabaseConfig config)
    : config_(config) {
    if (path == ":memory:") {
        path_         = ":memory:";
        is_in_memory_ = true;
        InitInMemory();
    } else {
        path_         = path;
        is_in_memory_ = false;
        InitPersistent(path);
    }
}

Database::~Database() {
    Shutdown();
}

void Database::InitInMemory() {
    buffer_manager_ = std::make_unique<BufferManager>(
        config_.buffer_pool_bytes, config_.block_size, nullptr);
    catalog_     = std::make_unique<Catalog>(*buffer_manager_);
    txn_manager_ = std::make_unique<TransactionManager>(*catalog_, nullptr);
    scheduler_   = std::make_unique<TaskScheduler>(config_.task_scheduler_threads);
    scheduler_->Initialize();
}

void Database::InitPersistent(const std::string& path) {
    block_file_ = std::make_unique<BlockFile>(path, config_.block_size);

    buffer_manager_ = std::make_unique<BufferManager>(
        config_.buffer_pool_bytes, config_.block_size, block_file_.get());

    std::string wal_path = path + ".wal";
    struct stat st{};
    bool wal_exists = (stat(wal_path.c_str(), &st) == 0 && st.st_size > 0);

    catalog_     = std::make_unique<Catalog>(*buffer_manager_);
    txn_manager_ = std::make_unique<TransactionManager>(*catalog_, nullptr);

    // Deserialize catalog from block 0 if the block file already has data.
    if (block_file_->BlockCount() > 0) {
        auto buf = std::make_unique<uint8_t[]>(config_.block_size);
        block_file_->ReadBlock(0, buf.get());
        catalog_->Deserialize(buf.get(), config_.block_size);
    }

    // Replay WAL entries written after the last checkpoint.
    if (wal_exists) {
        ReplayWAL();
    }

    // Now create the write-mode WAL (truncates existing file).
    wal_ = WAL::Create(wal_path);

    // Re-create TransactionManager with WAL for future writes.
    txn_manager_ = std::make_unique<TransactionManager>(*catalog_, wal_.get());

    ckpt_manager_ = std::make_unique<CheckpointManager>(
        *catalog_, *buffer_manager_, *wal_, *block_file_);

    scheduler_ = std::make_unique<TaskScheduler>(config_.task_scheduler_threads);
    scheduler_->Initialize();
}

void Database::ReplayWAL() {
    std::string wal_path = path_ + ".wal";
    auto replay_wal = WAL::OpenForReplay(wal_path);

    // Collect all entries, find the last checkpoint position.
    std::vector<WALEntry> entries;
    WALEntry entry;
    while (replay_wal->ReadNextEntry(entry)) {
        entries.push_back(std::move(entry));
        entry = WALEntry{};
    }
    replay_wal->Close();

    // Find the last WAL_CHECKPOINT marker index.
    size_t replay_start = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].type == WALEntryType::WAL_CHECKPOINT) {
            replay_start = i + 1;
        }
    }

    // Replay entries after the last checkpoint.
    for (size_t i = replay_start; i < entries.size(); ++i) {
        const WALEntry& e = entries[i];
        const uint8_t* p  = e.data.data();

        switch (e.type) {
            case WALEntryType::WAL_CREATE_TABLE: {
                std::string schema_name = RdStr(p);
                std::string table_name  = RdStr(p);
                uint32_t col_count      = RdU32(p);
                std::vector<ColumnDefinition> cols;
                cols.reserve(col_count);
                for (uint32_t c = 0; c < col_count; ++c) {
                    ColumnDefinition def;
                    def.name = RdStr(p);
                    def.type = static_cast<TypeId>(RdU8(p));
                    cols.push_back(std::move(def));
                }
                // Partition info (appended after columns).
                PartitionInfo pi;
                pi.type = (PartitionType)RdU8(p);
                if (pi.IsPartitioned()) {
                    uint32_t nkeys = RdU32(p);
                    for (uint32_t k = 0; k < nkeys; ++k) pi.partition_cols.push_back(RdStr(p));
                    pi.num_partitions = RdU32(p);
                    // Resolve partition_col_idxs from cols.
                    for (const auto& pcol : pi.partition_cols) {
                        for (int ci = 0; ci < (int)cols.size(); ++ci) {
                            if (cols[ci].name == pcol) {
                                pi.partition_col_idxs.push_back(ci); break;
                            }
                        }
                    }

                    auto RdVal = [&]() -> Value {
                        TypeId vtype = (TypeId)RdU8(p);
                        if (vtype == TypeId::VARCHAR) return Value::Varchar(RdStr(p));
                        if (vtype == TypeId::FLOAT32 || vtype == TypeId::FLOAT64) {
                            Value v = Value::Float(RdF64(p)); v.type = vtype; return v;
                        }
                        Value v = Value::Integer(RdI64(p)); v.type = vtype; return v;
                    };

                    if (pi.type == PartitionType::RANGE) {
                        uint32_t def_count = RdU32(p);
                        pi.defs.resize(def_count);
                        for (uint32_t di = 0; di < def_count; ++di) {
                            uint32_t nub = RdU32(p);
                            for (uint32_t k = 0; k < nub; ++k)
                                pi.defs[di].upper_bounds.push_back(RdVal());
                        }
                    } else if (pi.type == PartitionType::LIST) {
                        uint32_t def_count = RdU32(p);
                        pi.defs.resize(def_count);
                        for (uint32_t di = 0; di < def_count; ++di) {
                            uint32_t ntup = RdU32(p);
                            pi.defs[di].list_values.reserve(ntup);
                            for (uint32_t j = 0; j < ntup; ++j) {
                                uint32_t ncols = RdU32(p);
                                std::vector<Value> tuple;
                                for (uint32_t k = 0; k < ncols; ++k)
                                    tuple.push_back(RdVal());
                                pi.defs[di].list_values.push_back(std::move(tuple));
                            }
                        }
                    }
                }
                auto tx = txn_manager_->BeginTransaction(true);
                try {
                    catalog_->CreateTable(schema_name, table_name, cols, *tx, pi);
                    txn_manager_->Commit(tx);
                } catch (...) {
                    txn_manager_->Rollback(tx);
                }
                break;
            }

            case WALEntryType::WAL_DROP_TABLE: {
                std::string schema_name = RdStr(p);
                std::string table_name  = RdStr(p);
                auto tx = txn_manager_->BeginTransaction(true);
                try {
                    catalog_->DropTable(schema_name, table_name, *tx);
                    txn_manager_->Commit(tx);
                } catch (...) {
                    txn_manager_->Rollback(tx);
                }
                break;
            }

            case WALEntryType::WAL_INSERT: {
                std::string schema_name = RdStr(p);
                std::string table_name  = RdStr(p);
                DataChunk chunk = DeserializeChunkPayload(p);

                auto tx = txn_manager_->BeginTransaction(true);
                try {
                    auto* table = catalog_->GetTable(schema_name, table_name, *tx);
                    if (table) {
                        std::vector<size_t> col_ids(table->columns.size());
                        std::iota(col_ids.begin(), col_ids.end(), 0);

                        const auto& pi = table->partition_info;
                        if (!pi.IsPartitioned()) {
                            RowGroup* rg        = table->GetOrAddRowGroup();
                            size_t    rg_id     = table->row_groups.size() - 1;
                            size_t    app_start = rg->RowCount();
                            rg->Append(chunk, col_ids, tx->tx_id);

                            InsertUndoEntry ue;
                            ue.schema        = schema_name;
                            ue.table         = table_name;
                            ue.row_group_id  = rg_id;
                            ue.append_start  = app_start;
                            ue.append_count  = chunk.count;
                            ue.inserted_rows = chunk;
                            tx->undo_buffer.PushInsert(std::move(ue));
                        } else {
                            // Route rows by partition using all partition key columns.
                            const size_t nkeys = pi.partition_col_idxs.size();
                            std::unordered_map<uint32_t, std::vector<uint32_t>> by_part;
                            for (uint32_t r = 0; r < (uint32_t)chunk.count; ++r) {
                                // Stack-allocate key values (no heap alloc).
                                Value keys[8];
                                size_t used = std::min(nkeys, size_t(8));
                                for (size_t k = 0; k < used; ++k) {
                                    int col_idx = pi.partition_col_idxs[k];
                                    const auto& vec = chunk.columns[col_idx];
                                    TypeId t = vec.type;
                                    if (t == TypeId::FLOAT32 || t == TypeId::FLOAT64) {
                                        keys[k] = Value::Float(vec.float_data[r]);
                                        keys[k].type = t;
                                    } else if (t == TypeId::VARCHAR) {
                                        keys[k] = Value::Varchar(vec.str_data[r]);
                                    } else {
                                        keys[k] = Value::Integer(vec.int_data[r]);
                                        keys[k].type = t;
                                    }
                                }
                                by_part[pi.RouteRow(keys, used)].push_back(r);
                            }
                            for (auto& [pid, idxs] : by_part) {
                                DataChunk slice;
                                DataChunkSlice(slice, chunk, idxs);
                                RowGroup* rg        = table->GetOrAddRowGroupForPartition(pid);
                                size_t    rg_id     = table->partition_rg_indices[pid].back();
                                size_t    app_start = rg->RowCount();
                                rg->Append(slice, col_ids, tx->tx_id);

                                InsertUndoEntry ue;
                                ue.schema        = schema_name;
                                ue.table         = table_name;
                                ue.row_group_id  = rg_id;
                                ue.append_start  = app_start;
                                ue.append_count  = slice.count;
                                ue.inserted_rows = slice;
                                tx->undo_buffer.PushInsert(std::move(ue));
                            }
                        }
                    }
                    txn_manager_->Commit(tx);
                } catch (...) {
                    txn_manager_->Rollback(tx);
                }
                break;
            }

            case WALEntryType::WAL_DELETE: {
                std::string schema_name = RdStr(p);
                std::string table_name  = RdStr(p);
                uint32_t    count       = RdU32(p);
                std::vector<row_t> row_ids;
                row_ids.reserve(count);
                for (uint32_t i = 0; i < count; ++i) row_ids.push_back(RdU64(p));

                auto tx = txn_manager_->BeginTransaction(true);
                try {
                    auto* table = catalog_->GetTable(schema_name, table_name, *tx);
                    if (table) {
                        DeleteUndoEntry ue;
                        ue.schema  = schema_name;
                        ue.table   = table_name;
                        ue.row_ids = row_ids;
                        for (row_t rid : row_ids) {
                            uint32_t rg_idx = RowIdGroup(rid);
                            uint32_t offset = RowIdOffset(rid);
                            if (rg_idx < table->row_groups.size()) {
                                table->row_groups[rg_idx]->GetVersionInfo()
                                    .MarkDeleted(offset, tx->tx_id);
                            }
                        }
                        tx->undo_buffer.PushDelete(std::move(ue));
                    }
                    txn_manager_->Commit(tx);
                } catch (...) {
                    txn_manager_->Rollback(tx);
                }
                break;
            }

            case WALEntryType::WAL_UPDATE: {
                std::string schema_name = RdStr(p);
                std::string table_name  = RdStr(p);

                uint32_t col_count = RdU32(p);
                std::vector<size_t> col_ids;
                col_ids.reserve(col_count);
                for (uint32_t i = 0; i < col_count; ++i) {
                    col_ids.push_back(static_cast<size_t>(RdU32(p)));
                }

                uint32_t row_count = RdU32(p);
                std::vector<row_t> row_ids;
                row_ids.reserve(row_count);
                for (uint32_t i = 0; i < row_count; ++i) row_ids.push_back(RdU64(p));

                DataChunk new_values = DeserializeChunkPayload(p);

                auto tx = txn_manager_->BeginTransaction(true);
                try {
                    auto* table = catalog_->GetTable(schema_name, table_name, *tx);
                    if (table && row_count == new_values.count &&
                        col_count == new_values.ColumnCount()) {
                        std::vector<TypeId> update_types;
                        update_types.reserve(col_ids.size());
                        for (size_t col_id : col_ids) {
                            update_types.push_back(table->columns[col_id].type);
                        }

                        std::vector<row_t> applied_row_ids;
                        applied_row_ids.reserve(row_ids.size());

                        DataChunk old_values;
                        DataChunk applied_new_values;
                        old_values.Initialize(update_types);
                        applied_new_values.Initialize(update_types);

                        for (size_t ridx = 0; ridx < row_ids.size(); ++ridx) {
                            uint32_t rg_idx = RowIdGroup(row_ids[ridx]);
                            uint32_t offset = RowIdOffset(row_ids[ridx]);
                            if (rg_idx >= table->row_groups.size()) continue;
                            auto& rg = table->row_groups[rg_idx];
                            auto& chunks = rg->ColumnChunks();

                            for (size_t ui = 0; ui < col_ids.size(); ++ui) {
                                size_t col_id = col_ids[ui];
                                DataVector cur;
                                ColumnScanState s = chunks[col_id].MakeScanState(offset);
                                chunks[col_id].Scan(s, 1, cur);
                                DataVectorAppend(old_values.columns[ui], cur, 0);
                                chunks[col_id].WriteRow(offset, new_values.columns[ui], ridx);
                                DataVectorAppend(applied_new_values.columns[ui],
                                                 new_values.columns[ui], ridx);
                            }
                            old_values.count++;
                            applied_new_values.count++;
                            applied_row_ids.push_back(row_ids[ridx]);
                            rg->GetVersionInfo().MarkUpdated(offset, tx->tx_id);
                        }

                        if (!applied_row_ids.empty()) {
                            UpdateUndoEntry ue;
                            ue.schema = schema_name;
                            ue.table = table_name;
                            ue.row_ids = std::move(applied_row_ids);
                            ue.col_ids = col_ids;
                            ue.old_values = std::move(old_values);
                            ue.new_values = std::move(applied_new_values);
                            tx->undo_buffer.PushUpdate(std::move(ue));
                        }
                    }
                    txn_manager_->Commit(tx);
                } catch (...) {
                    txn_manager_->Rollback(tx);
                }
                break;
            }

            case WALEntryType::WAL_ALTER_TABLE: {
                std::string schema_name = RdStr(p);
                std::string table_name  = RdStr(p);
                // Read new partition_info (same format as WriteAlterTable).
                PartitionInfo pi;
                pi.type = (PartitionType)RdU8(p);
                if (pi.IsPartitioned()) {
                    uint32_t nkeys = RdU32(p);
                    for (uint32_t k = 0; k < nkeys; ++k) pi.partition_cols.push_back(RdStr(p));
                    pi.num_partitions = RdU32(p);
                    // Resolve indices from the existing table entry.
                    auto tx_tmp = txn_manager_->BeginTransaction(true);
                    auto* tbl = catalog_->GetTable(schema_name, table_name, *tx_tmp);
                    txn_manager_->Rollback(tx_tmp);
                    if (tbl) {
                        for (const auto& pcol : pi.partition_cols) {
                            for (int ci = 0; ci < (int)tbl->columns.size(); ++ci) {
                                if (tbl->columns[ci].name == pcol) {
                                    pi.partition_col_idxs.push_back(ci); break;
                                }
                            }
                        }
                    }

                    auto RdVal = [&]() -> Value {
                        TypeId vtype = (TypeId)RdU8(p);
                        if (vtype == TypeId::VARCHAR) return Value::Varchar(RdStr(p));
                        if (vtype == TypeId::FLOAT32 || vtype == TypeId::FLOAT64) {
                            Value v = Value::Float(RdF64(p)); v.type = vtype; return v;
                        }
                        Value v = Value::Integer(RdI64(p)); v.type = vtype; return v;
                    };

                    if (pi.type == PartitionType::RANGE) {
                        uint32_t def_count = RdU32(p);
                        pi.defs.resize(def_count);
                        for (uint32_t di = 0; di < def_count; ++di) {
                            uint32_t nub = RdU32(p);
                            for (uint32_t k = 0; k < nub; ++k)
                                pi.defs[di].upper_bounds.push_back(RdVal());
                        }
                    } else if (pi.type == PartitionType::LIST) {
                        uint32_t def_count = RdU32(p);
                        pi.defs.resize(def_count);
                        for (uint32_t di = 0; di < def_count; ++di) {
                            uint32_t ntup = RdU32(p);
                            pi.defs[di].list_values.reserve(ntup);
                            for (uint32_t j = 0; j < ntup; ++j) {
                                uint32_t ncols = RdU32(p);
                                std::vector<Value> tuple;
                                for (uint32_t k = 0; k < ncols; ++k)
                                    tuple.push_back(RdVal());
                                pi.defs[di].list_values.push_back(std::move(tuple));
                            }
                        }
                    }
                }
                // Read rg_indices.
                uint32_t part_count = RdU32(p);
                std::vector<std::vector<size_t>> rg_indices(part_count);
                for (uint32_t i = 0; i < part_count; ++i) {
                    uint32_t rg_count = RdU32(p);
                    rg_indices[i].reserve(rg_count);
                    for (uint32_t j = 0; j < rg_count; ++j)
                        rg_indices[i].push_back(static_cast<size_t>(RdU64(p)));
                }
                // Apply: restore partition state using system tx.
                {
                    auto tx = txn_manager_->BeginTransaction(true);
                    try {
                        catalog_->RestorePartitionState(schema_name, table_name, pi, rg_indices, *tx);
                        txn_manager_->Commit(tx);
                    } catch (...) {
                        txn_manager_->Rollback(tx);
                    }
                }
                break;
            }

            default:
                // Skip WAL_CREATE_SCHEMA, WAL_DROP_SCHEMA, etc.
                break;
        }
    }
}

void Database::Shutdown() {
    // Rollback any connections that still have active transactions.
    {
        std::lock_guard<std::mutex> lk(connections_mu_);
        // Connections handle their own rollback in their destructors;
        // we just need to ensure no new queries can start.
    }

    if (!is_in_memory_ && wal_ && ckpt_manager_) {
        try { wal_->Flush(); }       catch (...) {}
        try { ckpt_manager_->CreateCheckpoint(); } catch (...) {}
    }

    if (scheduler_) {
        try { scheduler_->Shutdown(); } catch (...) {}
    }

    if (buffer_manager_) {
        try { buffer_manager_->Shutdown(); } catch (...) {}
    }
}

std::unique_ptr<Connection> Database::Connect() {
    return std::make_unique<Connection>(*this);
}

BufferManager& Database::GetBufferManager() {
    return *buffer_manager_;
}

WAL& Database::GetWAL() {
    if (!wal_) throw RuntimeError("WAL not available in in-memory mode");
    return *wal_;
}

Catalog& Database::GetCatalog() {
    return *catalog_;
}

TransactionManager& Database::GetTransactionManager() {
    return *txn_manager_;
}

TaskScheduler& Database::GetTaskScheduler() {
    return *scheduler_;
}

void Database::RegisterConnection(Connection* conn) {
    std::lock_guard<std::mutex> lk(connections_mu_);
    open_connections_.push_back(conn);
}

void Database::UnregisterConnection(Connection* conn) {
    std::lock_guard<std::mutex> lk(connections_mu_);
    auto it = std::find(open_connections_.begin(), open_connections_.end(), conn);
    if (it != open_connections_.end()) {
        open_connections_.erase(it);
    }
}

} // namespace cppcoldb
