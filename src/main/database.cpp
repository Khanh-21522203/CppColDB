#include "main/database.hpp"
#include "main/connection.hpp"
#include "storage/buffer_manager.hpp"
#include "storage/block_file.hpp"
#include "storage/wal.hpp"
#include "catalog/catalog.hpp"
#include "catalog/catalog_entry.hpp"
#include "transaction/transaction_manager.hpp"
#include "transaction/transaction.hpp"
#include "checkpoint/checkpoint_manager.hpp"
#include "parallel/task_scheduler.hpp"
#include "storage/column/row_group.hpp"
#include "common/types.hpp"
#include "common/exception.hpp"
#include <sys/stat.h>
#include <cstring>
#include <algorithm>

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
// Decode a DataChunk from a WAL_INSERT payload
// ---------------------------------------------------------------------------
static DataChunk DeserializeChunk(const uint8_t* data, size_t /*size*/) {
    const uint8_t* p = data;

    /*std::string schema =*/ RdStr(p);
    /*std::string table  =*/ RdStr(p);

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
                auto tx = txn_manager_->BeginTransaction(true);
                try {
                    catalog_->CreateTable(schema_name, table_name, cols, *tx);
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
                // Peek at schema/table without advancing p permanently.
                const uint8_t* pp = p;
                std::string schema_name = RdStr(pp);
                std::string table_name  = RdStr(pp);

                DataChunk chunk = DeserializeChunk(p, e.data.size());

                auto tx = txn_manager_->BeginTransaction(true);
                try {
                    // Store locally so TransactionManager::Commit appends it.
                    std::string key = schema_name + "." + table_name;
                    tx->local_storage[key] = std::move(chunk);
                    txn_manager_->Commit(tx);
                } catch (...) {
                    txn_manager_->Rollback(tx);
                }
                break;
            }

            default:
                // Skip WAL_DELETE, WAL_UPDATE, WAL_CREATE_SCHEMA, etc.
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
