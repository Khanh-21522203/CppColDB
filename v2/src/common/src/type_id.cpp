#include "cppcoldb/common/types/type_id.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::common {

std::size_t TypeSize(TypeId) { CPPCOLDB_NOT_IMPLEMENTED(); }

std::string TypeName(TypeId) { CPPCOLDB_NOT_IMPLEMENTED(); }

TypeId TypeFromString(const std::string&) { CPPCOLDB_NOT_IMPLEMENTED(); }

} // namespace cppcoldb::common
