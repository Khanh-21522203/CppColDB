#include "main/client_context.hpp"
#include "parser/tokenizer.hpp"
#include "parser/parser.hpp"
#include "planner/binder.hpp"
#include "planner/optimizer.hpp"
#include "planner/physical_planner.hpp"
#include "execution/executor.hpp"
#include "catalog/catalog.hpp"
#include "transaction/transaction.hpp"
#include "common/exception.hpp"

namespace cppcoldb {

QueryResult ClientContext::Query(const std::string& sql) {
    try {
        Tokenizer tok(sql);
        auto tokens = tok.Tokenize();

        Parser parser(std::move(tokens));
        auto stmt = parser.Parse();

        Binder binder(*catalog, *transaction);
        auto logical = binder.Bind(*stmt);

        Optimizer opt;
        logical = opt.Optimize(std::move(logical));

        PhysicalPlanner pp;
        auto physical = pp.Plan(*logical);

        Executor exec(*this);
        exec.Initialize(std::move(physical));
        exec.Execute();

        return exec.GetResult();
    } catch (const CppColDBException& e) {
        QueryResult err;
        err.success       = false;
        err.error_message = e.what();
        return err;
    } catch (const std::exception& e) {
        QueryResult err;
        err.success       = false;
        err.error_message = e.what();
        return err;
    }
}

} // namespace cppcoldb
