#include "main/client_context.hpp"
#include "parser/tokenizer.hpp"
#include "parser/parser.hpp"
#include "parser/ast/parsed_statement.hpp"
#include "planner/binder.hpp"
#include "planner/optimizer.hpp"
#include "planner/physical_planner.hpp"
#include "execution/executor.hpp"
#include "catalog/catalog.hpp"
#include "transaction/transaction.hpp"
#include "common/exception.hpp"

namespace cppcoldb {

QueryResult ClientContext::Query(const std::string& sql) {
    bool explain_analyze = false;

    // RAII to restore profiling_enabled_ on both normal and exception paths.
    struct ProfRestore {
        bool& flag;
        bool  restore_to;
        ~ProfRestore() { flag = restore_to; }
    } prof_restore{profiling_enabled_, profiling_enabled_};

    try {
        // --- PARSE ---
        if (profiling_enabled_) {
            profiler_.StartQuery(sql);
            profiler_.StartPhase(QueryPhase::PARSE);
        }

        Tokenizer tok(sql);
        auto tokens = tok.Tokenize();
        Parser parser(std::move(tokens));
        auto stmt = parser.Parse();

        if (profiling_enabled_) profiler_.EndPhase(QueryPhase::PARSE);

        // --- EXPLAIN ANALYZE intercept ---
        ParsedStatement* inner_stmt = stmt.get();
        if (auto* explain = dynamic_cast<ExplainStatement*>(stmt.get())) {
            if (explain->analyze && explain->inner) {
                explain_analyze       = true;
                inner_stmt            = explain->inner.get();
                prof_restore.restore_to = false; // will restore to false at end

                if (!profiling_enabled_) {
                    // Force profiling on; retroactively emit a 0-duration PARSE phase.
                    profiling_enabled_ = true;
                    profiler_.StartQuery(sql);
                    profiler_.StartPhase(QueryPhase::PARSE);
                    profiler_.EndPhase(QueryPhase::PARSE);
                }
            }
        }

        // --- BIND ---
        if (profiling_enabled_) profiler_.StartPhase(QueryPhase::BIND);
        Binder binder(*catalog, *transaction);
        auto logical = binder.Bind(*inner_stmt);
        if (profiling_enabled_) profiler_.EndPhase(QueryPhase::BIND);

        // --- OPTIMIZE ---
        if (profiling_enabled_) profiler_.StartPhase(QueryPhase::OPTIMIZE);
        Optimizer opt;
        logical = opt.Optimize(std::move(logical));
        if (profiling_enabled_) profiler_.EndPhase(QueryPhase::OPTIMIZE);

        // --- PHYSICAL PLAN ---
        if (profiling_enabled_) profiler_.StartPhase(QueryPhase::PHYSICAL_PLAN);
        PhysicalPlanner pp;
        auto physical = pp.Plan(*logical);
        if (profiling_enabled_) profiler_.EndPhase(QueryPhase::PHYSICAL_PLAN);

        // --- EXECUTE ---
        Executor exec(*this);
        exec.Initialize(std::move(physical));

        if (profiling_enabled_) profiler_.StartPhase(QueryPhase::EXECUTE);
        exec.Execute();
        if (profiling_enabled_) profiler_.EndPhase(QueryPhase::EXECUTE);

        QueryResult result = exec.GetResult();

        // --- Finalize profiling ---
        if (profiling_enabled_) {
            auto prof_result = profiler_.EndQuery();

            if (explain_analyze) {
                // Return profiling text as single-column VARCHAR result.
                std::string plan_text = prof_result.ToString();
                QueryResult explain_out;
                explain_out.success       = true;
                explain_out.column_names  = {"QUERY PLAN"};
                explain_out.column_types  = {TypeId::VARCHAR};
                DataChunk chunk;
                chunk.Initialize({TypeId::VARCHAR});
                chunk.count = 1;
                chunk.columns[0].str_data.resize(1);
                chunk.columns[0].str_data[0] = std::move(plan_text);
                chunk.columns[0].validity.set(0);
                chunk.columns[0].count = 1;
                explain_out.chunks.push_back(std::move(chunk));
                explain_out.profiling_result = std::move(prof_result);
                return explain_out;
            }

            result.profiling_result = std::move(prof_result);
        }

        return result;

    } catch (const CppColDBException& e) {
        if (profiling_enabled_) profiler_.EndQuery(); // discard partial profile
        QueryResult err;
        err.success       = false;
        err.error_message = e.what();
        return err;
    } catch (const std::exception& e) {
        if (profiling_enabled_) profiler_.EndQuery();
        QueryResult err;
        err.success       = false;
        err.error_message = e.what();
        return err;
    }
}

} // namespace cppcoldb
