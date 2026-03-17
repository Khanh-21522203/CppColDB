#include "catalog/catalog.hpp"
#include "catalog/catalog_entry.hpp"
#include "storage/column/row_group.hpp"
#include "storage/column/column_segment.hpp"
#include "transaction/transaction.hpp"
#include "common/exception.hpp"
#include <cstring>

namespace cppcoldb {

// ---------------------------------------------------------------------------
// Checkpoint binary-format helpers
// ---------------------------------------------------------------------------

static void CkptWriteU8(uint8_t*& p, uint8_t v) { *p++ = v; }
static void CkptWriteU32(uint8_t*& p, uint32_t v) {
    for (int i = 0; i < 4; i++) { *p++ = (uint8_t)(v & 0xFF); v >>= 8; }
}
static void CkptWriteU64(uint8_t*& p, uint64_t v) {
    for (int i = 0; i < 8; i++) { *p++ = (uint8_t)(v & 0xFF); v >>= 8; }
}
static void CkptWriteStr(uint8_t*& p, const std::string& s) {
    uint16_t len = (uint16_t)s.size();
    *p++ = (uint8_t)(len & 0xFF); *p++ = (uint8_t)(len >> 8);
    std::memcpy(p, s.data(), len); p += len;
}

static uint8_t  CkptReadU8(const uint8_t*& p) { return *p++; }
static uint32_t CkptReadU32(const uint8_t*& p) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= ((uint32_t)*p++) << (8 * i);
    return v;
}
static uint64_t CkptReadU64(const uint8_t*& p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= ((uint64_t)*p++) << (8 * i);
    return v;
}
static std::string CkptReadStr(const uint8_t*& p) {
    uint16_t len = (uint16_t)*p++; len |= (uint16_t)(*p++) << 8;
    std::string s(reinterpret_cast<const char*>(p), len); p += len; return s;
}

Catalog::Catalog(BufferManager& bm) : bm_(bm) {
    // Create the default "main" schema.
    schemas_[DEFAULT_SCHEMA] = std::make_unique<Schema>(DEFAULT_SCHEMA);
}

void Catalog::CreateSchema(const std::string& name, const Transaction& /*tx*/) {
    std::lock_guard<std::mutex> lk(mu_);
    if (schemas_.count(name))
        throw BindError("Schema already exists: " + name);
    schemas_[name] = std::make_unique<Schema>(name);
}

void Catalog::DropSchema(const std::string& name, const Transaction& /*tx*/) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!schemas_.count(name))
        throw BindError("Schema not found: " + name);
    schemas_.erase(name);
}

Schema* Catalog::GetSchema(const std::string& name) const {
    auto it = schemas_.find(name);
    return it != schemas_.end() ? it->second.get() : nullptr;
}

CatalogEntry* Catalog::GetEntry(const std::string& schema,
                                 const std::string& name,
                                 const Transaction& tx,
                                 OnNotFound policy) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto* s = GetSchema(schema);
    if (!s) {
        if (policy == OnNotFound::THROW)
            throw BindError("Schema not found: " + schema);
        return nullptr;
    }
    return s->GetEntry(name, tx, policy);
}

TableCatalogEntry* Catalog::GetTable(const std::string& schema,
                                      const std::string& name,
                                      const Transaction& tx) const {
    // GetEntry handles locking internally.
    auto* entry = GetEntry(schema, name, tx, OnNotFound::RETURN_NULL);
    return dynamic_cast<TableCatalogEntry*>(entry);
}

void Catalog::CreateTable(const std::string& schema, const std::string& name,
                           const std::vector<ColumnDefinition>& cols,
                           const Transaction& tx) {
    std::lock_guard<std::mutex> lk(mu_);
    auto* s = GetSchema(schema);
    if (!s) throw BindError("Schema not found: " + schema);

    // MVCC duplicate check: throw if a visible entry already exists.
    // Call s->GetEntry directly (no nested lock).
    if (s->GetEntry(name, tx, OnNotFound::RETURN_NULL))
        throw BindError("Table already exists: " + schema + "." + name);

    auto entry              = std::make_unique<TableCatalogEntry>();
    entry->entry_type       = CatalogEntry::EntryType::TABLE;
    entry->name             = name;
    entry->schema_name      = schema;
    entry->columns          = cols;
    entry->create_tx_id     = tx.tx_id;
    entry->create_commit_time = INVALID_TIMESTAMP;
    entry->bm_              = &bm_;
    s->CreateEntry(std::move(entry));
}

void Catalog::DropTable(const std::string& schema, const std::string& name,
                         const Transaction& tx) {
    std::lock_guard<std::mutex> lk(mu_);
    auto* s = GetSchema(schema);
    if (!s) throw BindError("Schema not found: " + schema);
    // Call s->GetEntry directly (no nested lock).
    auto* entry = s->GetEntry(name, tx, OnNotFound::THROW);
    if (!entry) throw BindError("Table not found: " + schema + "." + name);
    s->MarkDeleted(name, tx);
}

void Catalog::CommitEntry(const std::string& schema, const std::string& name,
                           TransactionId tx_id, timestamp_t commit_time) {
    std::lock_guard<std::mutex> lk(mu_);
    auto* s = GetSchema(schema);
    if (s) s->CommitEntry(name, tx_id, commit_time);
}

void Catalog::RollbackCreate(const std::string& schema, const std::string& name,
                              TransactionId tx_id) {
    std::lock_guard<std::mutex> lk(mu_);
    auto* s = GetSchema(schema);
    if (s) s->RollbackCreate(name, tx_id);
}

void Catalog::RollbackDrop(const std::string& schema, const std::string& name,
                            TransactionId tx_id) {
    std::lock_guard<std::mutex> lk(mu_);
    auto* s = GetSchema(schema);
    if (s) s->RollbackDrop(name, tx_id);
}

void Catalog::Serialize(uint8_t* block, size_t /*block_size*/) const {
    std::lock_guard<std::mutex> lk(mu_);
    uint8_t* p = block;

    // Write schema count.
    CkptWriteU32(p, (uint32_t)schemas_.size());

    for (const auto& [sname, schema] : schemas_) {
        CkptWriteStr(p, sname);

        // Collect committed, non-deleted tables first to write an accurate count.
        std::vector<const TableCatalogEntry*> visible_tables;
        for (const auto& [tname, versions] : schema->Entries()) {
            for (const auto& entry_ptr : versions) {
                const auto* tbl = dynamic_cast<const TableCatalogEntry*>(entry_ptr.get());
                if (!tbl) continue;
                if (tbl->create_commit_time == INVALID_TIMESTAMP) continue;
                if (tbl->delete_commit_time != INVALID_TIMESTAMP) continue;
                visible_tables.push_back(tbl);
            }
        }

        CkptWriteU32(p, (uint32_t)visible_tables.size());

        for (const auto* tbl : visible_tables) {
            CkptWriteStr(p, tbl->name);

            // Columns.
            CkptWriteU32(p, (uint32_t)tbl->columns.size());
            for (const auto& col : tbl->columns) {
                CkptWriteStr(p, col.name);
                CkptWriteU8(p, (uint8_t)col.type);
                CkptWriteU8(p, col.not_null ? 1u : 0u);
            }

            // Row groups.
            CkptWriteU32(p, (uint32_t)tbl->row_groups.size());
            for (const auto& rg : tbl->row_groups) {
                CkptWriteU64(p, (uint64_t)rg->RowCount());

                const auto& chunks = rg->ColumnChunks();
                // col_count == tbl->columns.size(); iterate in order.
                for (const auto& chunk : chunks) {
                    const auto& segs = chunk.Segments();
                    CkptWriteU32(p, (uint32_t)segs.size());
                    for (const auto& seg : segs) {
                        CkptWriteU64(p, (uint64_t)seg.block_id);
                        CkptWriteU8(p, (uint8_t)seg.compression);
                        CkptWriteU8(p, (uint8_t)seg.column_type);
                        CkptWriteU32(p, seg.row_count);
                        CkptWriteU32(p, seg.row_offset);
                    }
                }
            }
        }
    }
}

void Catalog::Deserialize(const uint8_t* block, size_t /*block_size*/) {
    // System transaction: always visible (commit_time = 0, start_time = 0).
    Transaction sys_tx;
    sys_tx.tx_id      = INVALID_TRANSACTION;
    sys_tx.start_time = 0;
    sys_tx.commit_time = 0;

    const uint8_t* p = block;

    uint32_t schema_count = CkptReadU32(p);

    for (uint32_t si = 0; si < schema_count; si++) {
        std::string sname = CkptReadStr(p);

        // Ensure schema exists (may already have been created by constructor).
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (!schemas_.count(sname))
                schemas_[sname] = std::make_unique<Schema>(sname);
        }

        uint32_t table_count = CkptReadU32(p);

        for (uint32_t ti = 0; ti < table_count; ti++) {
            std::string tname = CkptReadStr(p);

            uint32_t col_count = CkptReadU32(p);
            std::vector<ColumnDefinition> cols;
            cols.reserve(col_count);
            for (uint32_t ci = 0; ci < col_count; ci++) {
                ColumnDefinition col;
                col.name     = CkptReadStr(p);
                col.type     = (TypeId)CkptReadU8(p);
                col.not_null = CkptReadU8(p) != 0;
                cols.push_back(std::move(col));
            }

            // Read row group metadata before creating table.
            struct RGMeta {
                uint64_t row_count;
                struct SegMeta {
                    BlockId         block_id;
                    CompressionType compression;
                    TypeId          column_type;
                    uint32_t        row_count;
                    uint32_t        row_offset;
                };
                std::vector<std::vector<SegMeta>> col_segments; // [col_idx][seg_idx]
            };
            uint32_t rg_count = CkptReadU32(p);
            std::vector<RGMeta> rg_metas;
            rg_metas.reserve(rg_count);
            for (uint32_t ri = 0; ri < rg_count; ri++) {
                RGMeta rg;
                rg.row_count = CkptReadU64(p);
                rg.col_segments.resize(col_count);
                for (uint32_t ci = 0; ci < col_count; ci++) {
                    uint32_t seg_count = CkptReadU32(p);
                    rg.col_segments[ci].reserve(seg_count);
                    for (uint32_t xi = 0; xi < seg_count; xi++) {
                        RGMeta::SegMeta sm;
                        sm.block_id    = (BlockId)CkptReadU64(p);
                        sm.compression = (CompressionType)CkptReadU8(p);
                        sm.column_type = (TypeId)CkptReadU8(p);
                        sm.row_count   = CkptReadU32(p);
                        sm.row_offset  = CkptReadU32(p);
                        rg.col_segments[ci].push_back(sm);
                    }
                }
                rg_metas.push_back(std::move(rg));
            }

            // Create empty table in catalog.
            CreateTable(sname, tname, cols, sys_tx);
            // Immediately commit it as always visible (commit_time = 1).
            CommitEntry(sname, tname, INVALID_TRANSACTION, 1);

            // Retrieve the freshly created entry and populate row groups.
            std::lock_guard<std::mutex> lk(mu_);
            auto* schema_ptr = GetSchema(sname);
            if (!schema_ptr) continue;

            // Find the entry directly (bypass MVCC by iterating entries).
            TableCatalogEntry* tbl_entry = nullptr;
            for (auto& [ename, versions] : const_cast<std::unordered_map<std::string,
                    std::vector<std::unique_ptr<CatalogEntry>>>&>(schema_ptr->Entries())) {
                if (ename != tname) continue;
                for (auto& ep : versions) {
                    auto* tbl = dynamic_cast<TableCatalogEntry*>(ep.get());
                    if (tbl) { tbl_entry = tbl; break; }
                }
                if (tbl_entry) break;
            }
            if (!tbl_entry) continue;

            // Collect column TypeIds for RowGroup construction.
            std::vector<TypeId> col_types;
            col_types.reserve(cols.size());
            for (const auto& c : cols) col_types.push_back(c.type);

            // Reconstruct row groups.
            tbl_entry->row_groups.clear();
            for (uint32_t ri = 0; ri < (uint32_t)rg_metas.size(); ri++) {
                const auto& rgm = rg_metas[ri];
                auto rg = std::make_unique<RowGroup>(ri, col_types, *tbl_entry->bm_);
                rg->SetRowCount((size_t)rgm.row_count);

                auto& chunks = rg->ColumnChunks();
                for (uint32_t ci = 0; ci < col_count; ci++) {
                    for (const auto& sm : rgm.col_segments[ci]) {
                        ColumnSegment seg;
                        seg.block_id    = sm.block_id;
                        seg.compression = sm.compression;
                        seg.column_type = sm.column_type;
                        seg.row_count   = sm.row_count;
                        seg.row_offset  = sm.row_offset;
                        chunks[ci].AddSegment(std::move(seg));
                    }
                }

                tbl_entry->row_groups.push_back(std::move(rg));
            }
        }
    }
}

} // namespace cppcoldb
