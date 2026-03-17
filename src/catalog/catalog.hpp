#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include "catalog/schema.hpp"

namespace cppcoldb {

class BufferManager;

class Catalog {
public:
    static constexpr const char* DEFAULT_SCHEMA = "main";

    explicit Catalog(BufferManager& bm);

    // Schema management
    void    CreateSchema(const std::string& name, const Transaction& tx);
    void    DropSchema  (const std::string& name, const Transaction& tx);
    Schema* GetSchema   (const std::string& name) const;

    // Entry lookup (MVCC-aware)
    CatalogEntry* GetEntry(const std::string& schema, const std::string& name,
                            const Transaction& tx,
                            OnNotFound policy = OnNotFound::THROW) const;
    TableCatalogEntry* GetTable(const std::string& schema, const std::string& name,
                                const Transaction& tx) const;

    // DDL operations
    void CreateTable(const std::string& schema, const std::string& name,
                     const std::vector<ColumnDefinition>& cols,
                     const Transaction& tx);
    void DropTable(const std::string& schema, const std::string& name,
                   const Transaction& tx);

    // Flush all committed row groups' pending data to compressed segments.
    // Must be called before Serialize() to ensure pending rows are persisted.
    void FlushAllRowGroups();
    bool HasUncommittedVersionMarkers() const;

    // Checkpoint: write catalog metadata to a raw block buffer.
    void Serialize(uint8_t* block, size_t block_size) const;

    // Checkpoint: reconstruct catalog from block buffer.
    // Uses a system transaction (always-visible entries).
    void Deserialize(const uint8_t* block, size_t block_size);

    // MVCC lifecycle — called from TransactionManager
    void CommitEntry  (const std::string& schema, const std::string& name,
                       TransactionId tx_id, timestamp_t commit_time);
    void RollbackCreate(const std::string& schema, const std::string& name,
                        TransactionId tx_id);
    void RollbackDrop  (const std::string& schema, const std::string& name,
                        TransactionId tx_id);

private:
    BufferManager& bm_;
    mutable std::mutex mu_;
    std::unordered_map<std::string, std::unique_ptr<Schema>> schemas_;
};

} // namespace cppcoldb
