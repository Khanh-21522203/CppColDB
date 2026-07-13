#include "cppcoldb/sql/parser/parser.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::sql::parser {

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

std::unique_ptr<ParsedStatement> Parser::Parse() { CPPCOLDB_NOT_IMPLEMENTED(); }

std::unique_ptr<ParsedStatement> Parser::ParseStatement() { CPPCOLDB_NOT_IMPLEMENTED(); }

std::unique_ptr<SelectStatement> Parser::ParseSelect() { CPPCOLDB_NOT_IMPLEMENTED(); }
std::unique_ptr<InsertStatement> Parser::ParseInsert() { CPPCOLDB_NOT_IMPLEMENTED(); }
std::unique_ptr<UpdateStatement> Parser::ParseUpdate() { CPPCOLDB_NOT_IMPLEMENTED(); }
std::unique_ptr<DeleteStatement> Parser::ParseDelete() { CPPCOLDB_NOT_IMPLEMENTED(); }
std::unique_ptr<CreateTableStatement> Parser::ParseCreateTable() { CPPCOLDB_NOT_IMPLEMENTED(); }
std::unique_ptr<DropTableStatement> Parser::ParseDropTable() { CPPCOLDB_NOT_IMPLEMENTED(); }
std::unique_ptr<AlterTableStatement> Parser::ParseAlterTable() { CPPCOLDB_NOT_IMPLEMENTED(); }
std::unique_ptr<TransactionStatement> Parser::ParseTransaction(ParsedStatement::Type) {
    CPPCOLDB_NOT_IMPLEMENTED();
}
std::unique_ptr<ExplainStatement> Parser::ParseExplain() { CPPCOLDB_NOT_IMPLEMENTED(); }

std::unique_ptr<Expr> Parser::ParseExpr() { CPPCOLDB_NOT_IMPLEMENTED(); }
std::unique_ptr<Expr> Parser::ParseOr() { CPPCOLDB_NOT_IMPLEMENTED(); }
std::unique_ptr<Expr> Parser::ParseAnd() { CPPCOLDB_NOT_IMPLEMENTED(); }
std::unique_ptr<Expr> Parser::ParseNot() { CPPCOLDB_NOT_IMPLEMENTED(); }
std::unique_ptr<Expr> Parser::ParseComparison() { CPPCOLDB_NOT_IMPLEMENTED(); }
std::unique_ptr<Expr> Parser::ParseAddSub() { CPPCOLDB_NOT_IMPLEMENTED(); }
std::unique_ptr<Expr> Parser::ParseMulDiv() { CPPCOLDB_NOT_IMPLEMENTED(); }
std::unique_ptr<Expr> Parser::ParseUnary() { CPPCOLDB_NOT_IMPLEMENTED(); }
std::unique_ptr<Expr> Parser::ParsePrimary() { CPPCOLDB_NOT_IMPLEMENTED(); }

void Parser::ParseQualifiedName(std::string&, std::string&) { CPPCOLDB_NOT_IMPLEMENTED(); }

common::TypeId Parser::ParseTypeName() { CPPCOLDB_NOT_IMPLEMENTED(); }

const Token& Parser::Peek() const { CPPCOLDB_NOT_IMPLEMENTED(); }
Token Parser::Consume() { CPPCOLDB_NOT_IMPLEMENTED(); }
Token Parser::Expect(TokenType, std::string_view) { CPPCOLDB_NOT_IMPLEMENTED(); }
bool Parser::Check(TokenType, std::string_view) const { CPPCOLDB_NOT_IMPLEMENTED(); }
bool Parser::Match(TokenType, std::string_view) { CPPCOLDB_NOT_IMPLEMENTED(); }

} // namespace cppcoldb::sql::parser
