#pragma once
#include <memory>
#include "planner/binder.hpp"

namespace cppcoldb {

class Catalog;
class Transaction;
struct ParsedStatement;

class LogicalPlanner {
public:
    LogicalPlanner(Catalog& catalog, Transaction& tx);
    std::unique_ptr<LogicalPlan> Plan(const ParsedStatement& stmt);

private:
    Binder binder_;
};

} // namespace cppcoldb
