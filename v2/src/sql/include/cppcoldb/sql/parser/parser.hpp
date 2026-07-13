#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "cppcoldb/common/types/type_id.hpp"
#include "cppcoldb/sql/parser/ast/parsed_statement.hpp"
#include "cppcoldb/sql/parser/token.hpp"

namespace cppcoldb::sql::parser {

// Recursive-descent parser: token stream -> ParsedStatement AST.
class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    // Parse the token stream into a single ParsedStatement.
    // Throws common::ParseError if the SQL is syntactically invalid or has trailing tokens.
    std::unique_ptr<ParsedStatement> Parse();

private:
    // Parse a statement without checking for trailing tokens.
    std::unique_ptr<ParsedStatement> ParseStatement();

    std::unique_ptr<SelectStatement>      ParseSelect();
    std::unique_ptr<InsertStatement>      ParseInsert();
    std::unique_ptr<UpdateStatement>      ParseUpdate();
    std::unique_ptr<DeleteStatement>      ParseDelete();
    std::unique_ptr<CreateTableStatement> ParseCreateTable();
    std::unique_ptr<DropTableStatement>   ParseDropTable();
    std::unique_ptr<AlterTableStatement>  ParseAlterTable();
    std::unique_ptr<TransactionStatement> ParseTransaction(ParsedStatement::Type type);
    std::unique_ptr<ExplainStatement>     ParseExplain();

    // Expression parsing -- recursive descent, lowest to highest precedence.
    std::unique_ptr<Expr> ParseExpr();
    std::unique_ptr<Expr> ParseOr();
    std::unique_ptr<Expr> ParseAnd();
    std::unique_ptr<Expr> ParseNot();
    std::unique_ptr<Expr> ParseComparison();
    std::unique_ptr<Expr> ParseAddSub();
    std::unique_ptr<Expr> ParseMulDiv();
    std::unique_ptr<Expr> ParseUnary();
    std::unique_ptr<Expr> ParsePrimary();

    // Parse [schema.]name; fills schema_out (may be empty) and name_out.
    void ParseQualifiedName(std::string& schema_out, std::string& name_out);

    // Parse a SQL type name keyword and return the corresponding TypeId.
    common::TypeId ParseTypeName();

    // Token stream helpers.
    const Token& Peek() const;
    Token        Consume();
    Token        Expect(TokenType t, std::string_view value = "");
    bool         Check(TokenType t, std::string_view value = "") const;
    bool         Match(TokenType t, std::string_view value = "");

    std::vector<Token> tokens_;
    std::size_t        pos_ = 0;
};

} // namespace cppcoldb::sql::parser
