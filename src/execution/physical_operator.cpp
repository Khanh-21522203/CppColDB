#include "execution/physical_operator.hpp"
#include "execution/pipeline.hpp"

namespace cppcoldb {

std::unique_ptr<OperatorState> PhysicalOperator::CreateScanState() const {
    return std::make_unique<ScanState>();
}

std::unique_ptr<OperatorState> PhysicalOperator::CreateOperatorState() const {
    return std::make_unique<OperatorState>();
}

std::unique_ptr<OperatorState> PhysicalOperator::CreateSinkState() const {
    return std::make_unique<OperatorState>();
}

} // namespace cppcoldb
