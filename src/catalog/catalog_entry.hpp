#pragma once
#include <string>
#include <vector>
#include <memory>
#include "common/types.hpp"

namespace cppcoldb {

class RowGroup;
class BufferManager;

struct ColumnDefinition {
    std::string name;
    TypeId      type;
    bool        not_null    = false;
    bool        primary_key = false;
};

// Base class for all catalog entries.
struct CatalogEntry {
    enum class EntryType { TABLE };

    EntryType   entry_type  = EntryType::TABLE;
    std::string name;
    std::string schema_name;

    TransactionId create_tx_id       = INVALID_TRANSACTION;
    timestamp_t   create_commit_time = INVALID_TIMESTAMP;
    TransactionId delete_tx_id       = INVALID_TRANSACTION;
    timestamp_t   delete_commit_time = INVALID_TIMESTAMP;

    virtual ~CatalogEntry() = default;
};

// A table entry in the catalog.
struct TableCatalogEntry : CatalogEntry {
    std::vector<ColumnDefinition>          columns;
    std::vector<std::unique_ptr<RowGroup>> row_groups; // owned storage
    BufferManager*                         bm_ = nullptr;

    size_t ColumnCount() const { return columns.size(); }
    int    FindColumn(const std::string& col_name) const;
    std::vector<std::string> ColumnNames() const;
    std::vector<TypeId>      ColumnTypes() const;

    // Return the last RowGroup if it has space, otherwise create a new one.
    RowGroup* GetOrAddRowGroup();
};

} // namespace cppcoldb
