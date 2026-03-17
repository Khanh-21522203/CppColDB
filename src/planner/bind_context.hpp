#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "common/types.hpp"

namespace cppcoldb {

struct ColumnBinding {
    size_t      table_idx   = 0;
    size_t      column_idx  = 0;
    TypeId      type        = TypeId::INVALID;
    std::string column_name;
    std::string table_name;
};

class BindContext {
public:
    // Register a table at table_idx with its column names and types.
    // col_idx_offset: added to each column's chunk position (for multi-table joins).
    void AddTable(size_t table_idx, const std::string& alias,
                  const std::vector<std::string>& col_names,
                  const std::vector<TypeId>&      col_types,
                  size_t col_idx_offset = 0);

    // Resolve an unqualified column name. Throws BindError on ambiguity or not-found.
    ColumnBinding ResolveColumn(const std::string& col_name) const;

    // Resolve a qualified "table.column" reference. Throws BindError if not found.
    ColumnBinding ResolveQualified(const std::string& table_alias,
                                   const std::string& col_name) const;

    // All registered table aliases (for ExpandStar).
    const std::unordered_map<std::string, std::vector<ColumnBinding>>& Tables() const {
        return tables_;
    }

    // Table aliases in insertion order (for deterministic ExpandStar with JOINs).
    const std::vector<std::string>& TableOrder() const { return table_order_; }

private:
    // key = column_name → list of ColumnBinding (multiple if same col name in different tables)
    std::unordered_map<std::string, std::vector<ColumnBinding>> columns_;
    // key = table_alias → ordered list of ColumnBindings for that table
    std::unordered_map<std::string, std::vector<ColumnBinding>> tables_;
    // insertion-ordered list of table aliases (for deterministic SELECT * expansion)
    std::vector<std::string> table_order_;
};

} // namespace cppcoldb
