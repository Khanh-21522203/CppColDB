#pragma once
#include <cstdint>

namespace cppcoldb::common {

// Transaction isolation level. v2 scaffold targets snapshot isolation by default.
enum class IsolationLevel : std::uint8_t {
    READ_UNCOMMITTED = 0,
    READ_COMMITTED   = 1,
    REPEATABLE_READ  = 2,
    SNAPSHOT         = 3,
    SERIALIZABLE     = 4,
};

} // namespace cppcoldb::common
