#include "planner/bind_context.hpp"
#include "common/exception.hpp"

namespace cppcoldb {

void BindContext::AddTable(size_t table_idx, const std::string& alias,
                           const std::vector<std::string>& col_names,
                           const std::vector<TypeId>&      col_types) {
    for (size_t i = 0; i < col_names.size(); ++i) {
        ColumnBinding b;
        b.table_idx   = table_idx;
        b.column_idx  = i;
        b.type        = col_types[i];
        b.column_name = col_names[i];
        b.table_name  = alias;

        columns_[col_names[i]].push_back(b);
        tables_[alias].push_back(b);
    }
}

ColumnBinding BindContext::ResolveColumn(const std::string& col_name) const {
    auto it = columns_.find(col_name);
    if (it == columns_.end() || it->second.empty()) {
        throw BindError("column not found: " + col_name);
    }
    if (it->second.size() > 1) {
        throw BindError("ambiguous column: " + col_name);
    }
    return it->second[0];
}

ColumnBinding BindContext::ResolveQualified(const std::string& table_alias,
                                            const std::string& col_name) const {
    auto it = tables_.find(table_alias);
    if (it == tables_.end()) {
        throw BindError("table not found in context: " + table_alias);
    }
    for (const auto& b : it->second) {
        if (b.column_name == col_name) {
            return b;
        }
    }
    throw BindError("column not found: " + table_alias + "." + col_name);
}

} // namespace cppcoldb
