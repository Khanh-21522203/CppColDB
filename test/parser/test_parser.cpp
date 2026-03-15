#include "parser/tokenizer.hpp"
#include "parser/parser.hpp"
#include "parser/ast/parsed_statement.hpp"
#include "common/exception.hpp"

#include <cassert>
#include <iostream>

using namespace cppcoldb;

// Helper: tokenize + parse in one step.
static std::unique_ptr<ParsedStatement> Parse(const std::string& sql) {
    Tokenizer tok(sql);
    Parser parser(tok.Tokenize());
    return parser.Parse();
}

// ---------------------------------------------------------------------------

static void TestParseSelect() {
    auto stmt = Parse("SELECT id, name FROM orders WHERE id > 5 LIMIT 10");
    assert(stmt->stmt_type == ParsedStatement::Type::SELECT);
    auto& sel = static_cast<SelectStatement&>(*stmt);
    assert(!sel.distinct);
    assert(sel.select_list.size() == 2);
    assert(sel.from_table == "orders");
    assert(sel.where_clause != nullptr);
    assert(sel.limit.has_value());
    assert(sel.limit.value() == 10);
    std::cout << "TestParseSelect: PASS\n";
}

static void TestParseSelectStar() {
    auto stmt = Parse("SELECT * FROM t");
    auto& sel = static_cast<SelectStatement&>(*stmt);
    assert(sel.select_list.size() == 1);
    assert(sel.select_list[0]->kind == Expr::Kind::STAR);
    std::cout << "TestParseSelectStar: PASS\n";
}

static void TestParseSelectDistinct() {
    auto stmt = Parse("SELECT DISTINCT a FROM t");
    auto& sel = static_cast<SelectStatement&>(*stmt);
    assert(sel.distinct);
    std::cout << "TestParseSelectDistinct: PASS\n";
}

static void TestParseInsertValues() {
    auto stmt = Parse("INSERT INTO t (a, b) VALUES (1, 'x'), (2, 'y')");
    assert(stmt->stmt_type == ParsedStatement::Type::INSERT);
    auto& ins = static_cast<InsertStatement&>(*stmt);
    assert(ins.table_name == "t");
    assert(ins.column_names.size() == 2);
    assert(ins.column_names[0] == "a");
    assert(ins.column_names[1] == "b");
    assert(ins.values.size() == 2);
    assert(ins.values[0].size() == 2);
    assert(ins.values[1].size() == 2);
    std::cout << "TestParseInsertValues: PASS\n";
}

static void TestParseUpdate() {
    auto stmt = Parse("UPDATE t SET a = 1 WHERE b = 2");
    assert(stmt->stmt_type == ParsedStatement::Type::UPDATE);
    auto& upd = static_cast<UpdateStatement&>(*stmt);
    assert(upd.table_name == "t");
    assert(upd.set_clauses.size() == 1);
    assert(upd.set_clauses[0].column_name == "a");
    assert(upd.where_clause != nullptr);
    std::cout << "TestParseUpdate: PASS\n";
}

static void TestParseDelete() {
    auto stmt = Parse("DELETE FROM t WHERE id = 3");
    assert(stmt->stmt_type == ParsedStatement::Type::DELETE);
    auto& del = static_cast<DeleteStatement&>(*stmt);
    assert(del.table_name == "t");
    assert(del.where_clause != nullptr);
    std::cout << "TestParseDelete: PASS\n";
}

static void TestParseCreateTable() {
    auto stmt = Parse(
        "CREATE TABLE s.t (id INT64 PRIMARY KEY, name VARCHAR NOT NULL)");
    assert(stmt->stmt_type == ParsedStatement::Type::CREATE_TABLE);
    auto& ct = static_cast<CreateTableStatement&>(*stmt);
    assert(ct.schema_name == "s");
    assert(ct.table_name == "t");
    assert(ct.columns.size() == 2);
    assert(ct.columns[0].name == "id");
    assert(ct.columns[0].type == TypeId::INT64);
    assert(ct.columns[0].primary_key);
    assert(ct.columns[0].not_null);
    assert(ct.columns[1].name == "name");
    assert(ct.columns[1].type == TypeId::VARCHAR);
    assert(ct.columns[1].not_null);
    assert(!ct.columns[1].primary_key);
    std::cout << "TestParseCreateTable: PASS\n";
}

static void TestParseDropTableIfExists() {
    auto stmt = Parse("DROP TABLE IF EXISTS t");
    assert(stmt->stmt_type == ParsedStatement::Type::DROP_TABLE);
    auto& dt = static_cast<DropTableStatement&>(*stmt);
    assert(dt.if_exists);
    assert(dt.table_name == "t");
    std::cout << "TestParseDropTableIfExists: PASS\n";
}

static void TestParseBeginCommitRollback() {
    auto b = Parse("BEGIN");
    assert(b->stmt_type == ParsedStatement::Type::BEGIN);

    auto c = Parse("COMMIT");
    assert(c->stmt_type == ParsedStatement::Type::COMMIT);

    auto r = Parse("ROLLBACK");
    assert(r->stmt_type == ParsedStatement::Type::ROLLBACK);

    std::cout << "TestParseBeginCommitRollback: PASS\n";
}

static void TestParseExplainAnalyze() {
    auto stmt = Parse("EXPLAIN ANALYZE SELECT 1 FROM t");
    assert(stmt->stmt_type == ParsedStatement::Type::EXPLAIN);
    auto& ex = static_cast<ExplainStatement&>(*stmt);
    assert(ex.analyze);
    assert(ex.inner != nullptr);
    assert(ex.inner->stmt_type == ParsedStatement::Type::SELECT);
    std::cout << "TestParseExplainAnalyze: PASS\n";
}

static void TestParseExprPrecedence() {
    // 1 + 2 * 3  →  BinaryOp(+, 1, BinaryOp(*, 2, 3))
    auto stmt = Parse("SELECT 1 + 2 * 3 FROM t");
    auto& sel = static_cast<SelectStatement&>(*stmt);
    auto& top = static_cast<BinaryOpExpr&>(*sel.select_list[0]);
    assert(top.op == "+");
    assert(top.left->kind == Expr::Kind::INTEGER_LIT);
    assert(top.right->kind == Expr::Kind::BINARY_OP);
    auto& rhs = static_cast<BinaryOpExpr&>(*top.right);
    assert(rhs.op == "*");
    std::cout << "TestParseExprPrecedence: PASS\n";
}

static void TestParseTrailingTokensError() {
    bool threw = false;
    try {
        Parse("SELECT 1 FROM t GARBAGE");
    } catch (const ParseError&) {
        threw = true;
    }
    assert(threw);
    std::cout << "TestParseTrailingTokensError: PASS\n";
}

static void TestParseUnknownStatementError() {
    bool threw = false;
    try {
        Parse("FOOBAR");
    } catch (const ParseError&) {
        threw = true;
    }
    assert(threw);
    std::cout << "TestParseUnknownStatementError: PASS\n";
}

static void TestParseSelectGroupByOrderBy() {
    auto stmt = Parse("SELECT a, b FROM t GROUP BY a ORDER BY b DESC");
    auto& sel = static_cast<SelectStatement&>(*stmt);
    assert(sel.group_by.size() == 1);
    assert(sel.order_by.size() == 1);
    assert(sel.order_asc.size() == 1);
    assert(sel.order_asc[0] == false); // DESC
    std::cout << "TestParseSelectGroupByOrderBy: PASS\n";
}

static void TestParseFunctionCall() {
    auto stmt = Parse("SELECT COUNT(*), SUM(price) FROM orders");
    auto& sel = static_cast<SelectStatement&>(*stmt);
    assert(sel.select_list.size() == 2);
    auto& count = static_cast<FunctionCallExpr&>(*sel.select_list[0]);
    assert(count.kind == Expr::Kind::FUNCTION_CALL);
    assert(count.name == "COUNT");
    assert(count.is_star);
    auto& sum = static_cast<FunctionCallExpr&>(*sel.select_list[1]);
    assert(sum.name == "SUM");
    assert(!sum.is_star);
    assert(sum.args.size() == 1);
    std::cout << "TestParseFunctionCall: PASS\n";
}

static void TestParseQualifiedColumnRef() {
    auto stmt = Parse("SELECT t.id FROM t WHERE t.id > 0");
    auto& sel = static_cast<SelectStatement&>(*stmt);
    auto& col = static_cast<ColumnRefExpr&>(*sel.select_list[0]);
    assert(col.table_name == "t");
    assert(col.column_name == "id");
    std::cout << "TestParseQualifiedColumnRef: PASS\n";
}

static void TestParseTrailingSemicolon() {
    // Should not throw
    auto stmt = Parse("SELECT 1 FROM t;");
    assert(stmt->stmt_type == ParsedStatement::Type::SELECT);
    std::cout << "TestParseTrailingSemicolon: PASS\n";
}

void RunParserTests() {
    TestParseSelect();
    TestParseSelectStar();
    TestParseSelectDistinct();
    TestParseInsertValues();
    TestParseUpdate();
    TestParseDelete();
    TestParseCreateTable();
    TestParseDropTableIfExists();
    TestParseBeginCommitRollback();
    TestParseExplainAnalyze();
    TestParseExprPrecedence();
    TestParseTrailingTokensError();
    TestParseUnknownStatementError();
    TestParseSelectGroupByOrderBy();
    TestParseFunctionCall();
    TestParseQualifiedColumnRef();
    TestParseTrailingSemicolon();
    std::cout << "\nAll parser tests passed.\n";
}
