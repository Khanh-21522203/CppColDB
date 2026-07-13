#pragma once
#include <cstddef>
#include <string>

#include "cppcoldb/common/constants.hpp"

namespace cppcoldb {

// Construction-time configuration for a Database. Header-only value type;
// carries no behavior. Database::Database(path, config) takes the on-disk
// path explicitly as well; `path` here is retained for callers that build a
// DatabaseConfig standalone (e.g. from a config file) before opening it.
struct DatabaseConfig {
    std::string path;        // on-disk path; ignored when in_memory == true
    bool        in_memory = false;

    std::size_t buffer_pool_size = 256ULL * 1024 * 1024; // 256 MiB
    std::size_t block_size       = common::DEFAULT_BLOCK_SIZE;
};

} // namespace cppcoldb
