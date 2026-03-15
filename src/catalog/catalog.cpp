#include "catalog/catalog.hpp"
#include "catalog/catalog_entry.hpp"
#include "storage/column/row_group.hpp"
#include "transaction/transaction.hpp"
#include "common/exception.hpp"

namespace cppcoldb {

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

} // namespace cppcoldb
