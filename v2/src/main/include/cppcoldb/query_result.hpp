#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "cppcoldb/common/types/data_chunk.hpp"
#include "cppcoldb/common/types/type_id.hpp"

namespace cppcoldb {

// The public-facing result of Connection::Query() / ClientContext::Query().
// Self-contained in the root namespace: it does NOT reuse
// engine::execution::QueryResult, which is an internal type owned by the
// executor and not part of the stable public API.
struct QueryResult {
    bool        success = true;
    std::string error_message;

    std::vector<std::string>       column_names;
    std::vector<common::TypeId>    column_types;
    std::vector<common::DataChunk> chunks;

    std::size_t RowCount() const {
        std::size_t total = 0;
        for (const auto& c : chunks) total += c.count;
        return total;
    }
};

} // namespace cppcoldb
