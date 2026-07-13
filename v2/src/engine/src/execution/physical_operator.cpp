#include "cppcoldb/engine/execution/physical_operator.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::execution {

std::unique_ptr<OperatorState> PhysicalOperator::CreateScanState() const { CPPCOLDB_NOT_IMPLEMENTED(); }
std::unique_ptr<OperatorState> PhysicalOperator::CreateOperatorState() const { CPPCOLDB_NOT_IMPLEMENTED(); }
std::unique_ptr<OperatorState> PhysicalOperator::CreateSinkState() const { CPPCOLDB_NOT_IMPLEMENTED(); }

} // namespace cppcoldb::engine::execution
