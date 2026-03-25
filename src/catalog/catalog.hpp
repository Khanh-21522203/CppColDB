#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include "catalog/schema.hpp"
#include "storage/partition_info.hpp"

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
                     const Transaction& tx,
                     const PartitionInfo& partition_info = PartitionInfo{});
    void DropTable(const std::string& schema, const std::string& name,
                   const Transaction& tx);

    // ALTER TABLE DROP/ADD PARTITION.
    // Returns old state in *_out for undo. Sets new state directly on the entry.
    void DropPartition(const std::string& schema, const std::string& name,
                       uint32_t partition_idx, const Transaction& tx,
                       PartitionInfo& old_pi_out,
                       std::vector<std::vector<size_t>>& old_rg_out);
    void AddPartition  (const std::string& schema, const std::string& name,
                        const std::vector<Value>& new_bound,          // RANGE: one per key col
                        const std::vector<std::vector<Value>>& new_values, // LIST: tuples
                        const Transaction& tx,
                        PartitionInfo& old_pi_out,
                        std::vector<std::vector<size_t>>& old_rg_out);
    // Restore partition state directly (used for rollback and WAL replay).
    void RestorePartitionState(const std::string& schema, const std::string& name,
                                const PartitionInfo& pi,
                                const std::vector<std::vector<size_t>>& rg_indices,
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
