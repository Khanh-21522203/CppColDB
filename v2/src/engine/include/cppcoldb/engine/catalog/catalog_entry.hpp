#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cppcoldb/common/types/timestamp.hpp"
#include "cppcoldb/common/types/transaction_id.hpp"
#include "cppcoldb/common/types/type_id.hpp"

namespace cppcoldb::engine::catalog {

// Definition of a single table column.
struct ColumnDefinition {
    std::string    name;
    common::TypeId type = common::TypeId::INVALID;
    bool           not_null = false;
    bool           primary_key = false;
};

// Base class for all catalog entries. Carries the MVCC create/delete
// bookkeeping shared by every entry kind.
struct CatalogEntry {
    // Tag hierarchy for catalog entry kinds (mirrors v1; TABLE is the only
    // kind scaffolded so far — views/indexes extend this enum later).
    enum class EntryType : std::uint8_t { TABLE };

    EntryType   entry_type = EntryType::TABLE;
    std::string name;
    std::string schema_name;

    common::TransactionId create_tx_id = common::INVALID_TRANSACTION;
    common::Timestamp     create_commit_time = common::INVALID_TIMESTAMP;
    common::TransactionId delete_tx_id = common::INVALID_TRANSACTION;
    common::Timestamp     delete_commit_time = common::INVALID_TIMESTAMP;

    CatalogEntry() = default;
    explicit CatalogEntry(EntryType type) : entry_type(type) {}
    virtual ~CatalogEntry() = default;
};

// A table entry in the catalog: its column definitions in declared order.
struct TableCatalogEntry : CatalogEntry {
    TableCatalogEntry() : CatalogEntry(EntryType::TABLE) {}

    std::vector<ColumnDefinition> columns;

    std::size_t ColumnCount() const { return columns.size(); }

    // Returns the ordinal of col_name, or -1 if not found.
    int FindColumn(const std::string& col_name) const;
    std::vector<std::string>    ColumnNames() const;
    std::vector<common::TypeId> ColumnTypes() const;
};

} // namespace cppcoldb::engine::catalog
