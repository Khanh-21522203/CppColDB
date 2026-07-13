#pragma once
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "cppcoldb/common/types/type_id.hpp"

namespace cppcoldb::sql::planner {

// A single resolved column: which table/position it lives at in the bind
// context, plus its display name and type.
struct ColumnBinding {
    std::size_t    table_idx  = 0;
    std::size_t    column_idx = 0;
    common::TypeId type       = common::TypeId::INVALID;
    std::string    column_name;
    std::string    table_name;
};

// Tracks the tables/columns visible while binding a single SELECT's
// expressions (including joined tables).
class BindContext {
public:
    // Register a table at table_idx with its column names and types.
    // col_idx_offset: added to each column's chunk position (for multi-table joins).
    void AddTable(std::size_t table_idx, const std::string& alias,
                  const std::vector<std::string>& col_names,
                  const std::vector<common::TypeId>& col_types,
                  std::size_t col_idx_offset = 0);

    // Resolve an unqualified column name. Throws common::BindError on ambiguity or not-found.
    ColumnBinding ResolveColumn(const std::string& col_name) const;

    // Resolve a qualified "table.column" reference. Throws common::BindError if not found.
    ColumnBinding ResolveQualified(const std::string& table_alias,
                                   const std::string& col_name) const;

    // All registered table aliases (for ExpandStar).
    const std::unordered_map<std::string, std::vector<ColumnBinding>>& Tables() const {
        return tables_;
    }

    // Table aliases in insertion order (for deterministic ExpandStar with JOINs).
    const std::vector<std::string>& TableOrder() const { return table_order_; }

private:
    // key = column_name -> list of ColumnBinding (multiple if same col name in different tables)
    std::unordered_map<std::string, std::vector<ColumnBinding>> columns_;
    // key = table_alias -> ordered list of ColumnBindings for that table
    std::unordered_map<std::string, std::vector<ColumnBinding>> tables_;
    // insertion-ordered list of table aliases (for deterministic SELECT * expansion)
    std::vector<std::string> table_order_;
};

} // namespace cppcoldb::sql::planner
