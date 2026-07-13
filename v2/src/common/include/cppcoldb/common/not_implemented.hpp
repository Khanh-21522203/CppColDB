#pragma once
#include <source_location>
#include <string>

#include "cppcoldb/common/exception.hpp"

namespace cppcoldb::common {

// Thrown by CPPCOLDB_NOT_IMPLEMENTED() in scaffold stub bodies.
class NotImplementedError : public CppColDBException {
public:
    explicit NotImplementedError(std::string msg) : CppColDBException(std::move(msg)) {}
    std::string Type() const override { return "NotImplementedError"; }
};

// Marks a stub body. Declarations are complete; bodies call this until implemented.
[[noreturn]] inline void ThrowNotImplemented(
    const std::source_location loc = std::source_location::current()) {
    throw NotImplementedError(std::string("not implemented: ") + loc.function_name());
}

} // namespace cppcoldb::common

#define CPPCOLDB_NOT_IMPLEMENTED() ::cppcoldb::common::ThrowNotImplemented()
