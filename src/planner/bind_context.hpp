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
    void AddTable(size_t table_idx, const std::string& alias,
                  const std::vector<std::string>& col_names,
                  const std::vector<TypeId>&      col_types);

    // Resolve an unqualified column name. Throws BindError on ambiguity or not-found.
    ColumnBinding ResolveColumn(const std::string& col_name) const;

    // Resolve a qualified "table.column" reference. Throws BindError if not found.
    ColumnBinding ResolveQualified(const std::string& table_alias,
                                   const std::string& col_name) const;

    // All registered table aliases (for ExpandStar).
    const std::unordered_map<std::string, std::vector<ColumnBinding>>& Tables() const {
        return tables_;
    }

private:
    // key = column_name → list of ColumnBinding (multiple if same col name in different tables)
    std::unordered_map<std::string, std::vector<ColumnBinding>> columns_;
    // key = table_alias → ordered list of ColumnBindings for that table
    std::unordered_map<std::string, std::vector<ColumnBinding>> tables_;
};

} // namespace cppcoldb
