#include "cppcoldb/common/types/value.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::common {

std::int64_t Value::GetInt64() const { CPPCOLDB_NOT_IMPLEMENTED(); }

double Value::GetFloat64() const { CPPCOLDB_NOT_IMPLEMENTED(); }

bool Value::GetBool() const { CPPCOLDB_NOT_IMPLEMENTED(); }

const std::string& Value::GetVarchar() const { CPPCOLDB_NOT_IMPLEMENTED(); }

std::string Value::ToString() const { CPPCOLDB_NOT_IMPLEMENTED(); }

bool Value::operator==(const Value&) const { CPPCOLDB_NOT_IMPLEMENTED(); }

bool Value::operator<(const Value&) const { CPPCOLDB_NOT_IMPLEMENTED(); }

std::size_t ValueHash(const Value&) { CPPCOLDB_NOT_IMPLEMENTED(); }

} // namespace cppcoldb::common
