#include "parser/parser.hpp"
#include "common/exception.hpp"

#include <stdexcept>
#include <charconv>
#include <algorithm>
#include <cctype>

namespace cppcoldb {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string ToUpper(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

const Token& Parser::Peek() const {
    return tokens_[pos_];
}

Token Parser::Consume() {
    Token t = tokens_[pos_];
    if (t.type != TokenType::END_OF_INPUT) ++pos_;
    return t;
}

bool Parser::Check(TokenType t, std::string_view value) const {
    const Token& tok = tokens_[pos_];
    if (tok.type != t) return false;
    if (value.empty()) return true;
    // Case-insensitive comparison for keywords; exact for others.
    if (t == TokenType::KEYWORD) return ToUpper(tok.value) == ToUpper(value);
    return tok.value == value;
}

bool Parser::Match(TokenType t, std::string_view value) {
    if (!Check(t, value)) return false;
    Consume();
    return true;
}

Token Parser::Expect(TokenType t, std::string_view value) {
    if (!Check(t, value)) {
        const Token& tok = tokens_[pos_];
        std::string msg = "expected ";
        if (!value.empty()) msg += "'" + std::string(value) + "'";
        else msg += "token type " + std::to_string(static_cast<int>(t));
        msg += " but got '" + tok.value + "'";
        throw ParseError(msg, tok.pos);
    }
    return Consume();
}

// ---------------------------------------------------------------------------
// Qualified name: [schema.]name
// ---------------------------------------------------------------------------

void Parser::ParseQualifiedName(std::string& schema_out, std::string& name_out) {
    // Accept identifiers OR type-name keywords used as table/column names.
    // First token must be an identifier.
    Token first = Expect(TokenType::IDENTIFIER);
    if (Check(TokenType::PUNCTUATION, ".")) {
        Consume(); // consume '.'
        Token second = Expect(TokenType::IDENTIFIER);
        schema_out = first.value;
        name_out   = second.value;
    } else {
        schema_out = "";
        name_out   = first.value;
    }
}

// ---------------------------------------------------------------------------
// Type name keyword → TypeId
// ---------------------------------------------------------------------------

TypeId Parser::ParseTypeName() {
    const Token& tok = Peek();
    if (tok.type != TokenType::KEYWORD && tok.type != TokenType::IDENTIFIER) {
        throw ParseError("expected type name", tok.pos);
    }
    std::string upper = ToUpper(tok.value);
    Consume();

    if (upper == "INT8")    return TypeId::INT8;
    if (upper == "INT16")   return TypeId::INT16;
    if (upper == "INT32")   return TypeId::INT32;
    if (upper == "INT64")   return TypeId::INT64;
    if (upper == "INT" || upper == "INTEGER") return TypeId::INT64;
    if (upper == "FLOAT32") return TypeId::FLOAT32;
    if (upper == "FLOAT64") return TypeId::FLOAT64;
    if (upper == "FLOAT" || upper == "DOUBLE") return TypeId::FLOAT64;
    if (upper == "BOOLEAN" || upper == "BOOL") return TypeId::BOOLEAN;
    if (upper == "VARCHAR" || upper == "TEXT") return TypeId::VARCHAR;

    throw ParseError("unknown type name '" + tok.value + "'", tok.pos);
}

// ---------------------------------------------------------------------------
// Top-level entry point
// ---------------------------------------------------------------------------

std::unique_ptr<ParsedStatement> Parser::Parse() {
    auto stmt = ParseStatement();
    // Allow optional trailing semicolon.
    Match(TokenType::PUNCTUATION, ";");
    if (!Check(TokenType::END_OF_INPUT)) {
        const Token& tok = Peek();
        throw ParseError("unexpected trailing token '" + tok.value + "'", tok.pos);
    }
    return stmt;
}

std::unique_ptr<ParsedStatement> Parser::ParseStatement() {
    const Token& tok = Peek();

    if (tok.type == TokenType::KEYWORD) {
        if (tok.value == "SELECT")   return ParseSelect();
        if (tok.value == "INSERT")   return ParseInsert();
        if (tok.value == "UPDATE")   return ParseUpdate();
        if (tok.value == "DELETE")   return ParseDelete();
        if (tok.value == "CREATE")   return ParseCreateTable();
        if (tok.value == "DROP")     return ParseDropTable();
        if (tok.value == "BEGIN")    return ParseTransaction(ParsedStatement::Type::BEGIN);
        if (tok.value == "COMMIT")   return ParseTransaction(ParsedStatement::Type::COMMIT);
        if (tok.value == "ROLLBACK") return ParseTransaction(ParsedStatement::Type::ROLLBACK);
        if (tok.value == "EXPLAIN")  return ParseExplain();
    }

    throw ParseError("unexpected token '" + tok.value + "'", tok.pos);
}

// ---------------------------------------------------------------------------
// SELECT
// ---------------------------------------------------------------------------

std::unique_ptr<SelectStatement> Parser::ParseSelect() {
    Expect(TokenType::KEYWORD, "SELECT");
    auto stmt = std::make_unique<SelectStatement>();

    if (Match(TokenType::KEYWORD, "DISTINCT"))
        stmt->distinct = true;

    // Select list: comma-separated expressions.
    stmt->select_list.push_back(ParseExpr());
    while (Match(TokenType::PUNCTUATION, ","))
        stmt->select_list.push_back(ParseExpr());

    Expect(TokenType::KEYWORD, "FROM");
    ParseQualifiedName(stmt->from_schema, stmt->from_table);

    // Parse optional [INNER] JOIN clauses.
    while (Check(TokenType::KEYWORD, "INNER") || Check(TokenType::KEYWORD, "JOIN")) {
        Match(TokenType::KEYWORD, "INNER"); // consume optional INNER
        Expect(TokenType::KEYWORD, "JOIN");
        JoinClause jc;
        ParseQualifiedName(jc.schema_name, jc.table_name);
        Expect(TokenType::KEYWORD, "ON");
        jc.condition = ParseExpr();
        stmt->joins.push_back(std::move(jc));
    }

    if (Match(TokenType::KEYWORD, "WHERE"))
        stmt->where_clause = ParseExpr();

    if (Match(TokenType::KEYWORD, "GROUP")) {
        Expect(TokenType::KEYWORD, "BY");
        stmt->group_by.push_back(ParseExpr());
        while (Match(TokenType::PUNCTUATION, ","))
            stmt->group_by.push_back(ParseExpr());
    }

    if (Match(TokenType::KEYWORD, "ORDER")) {
        Expect(TokenType::KEYWORD, "BY");
        do {
            stmt->order_by.push_back(ParseExpr());
            if (Match(TokenType::KEYWORD, "DESC"))
                stmt->order_asc.push_back(false);
            else {
                Match(TokenType::KEYWORD, "ASC"); // optional
                stmt->order_asc.push_back(true);
            }
        } while (Match(TokenType::PUNCTUATION, ","));
    }

    if (Match(TokenType::KEYWORD, "LIMIT")) {
        Token t = Expect(TokenType::INTEGER_LIT);
        stmt->limit = std::stoll(t.value);
    }

    if (Match(TokenType::KEYWORD, "OFFSET")) {
        Token t = Expect(TokenType::INTEGER_LIT);
        stmt->offset = std::stoll(t.value);
    }

    return stmt;
}

// ---------------------------------------------------------------------------
// INSERT
// ---------------------------------------------------------------------------

std::unique_ptr<InsertStatement> Parser::ParseInsert() {
    Expect(TokenType::KEYWORD, "INSERT");
    Expect(TokenType::KEYWORD, "INTO");
    auto stmt = std::make_unique<InsertStatement>();

    ParseQualifiedName(stmt->schema_name, stmt->table_name);

    // Optional column list.
    if (Match(TokenType::PUNCTUATION, "(")) {
        stmt->column_names.push_back(Expect(TokenType::IDENTIFIER).value);
        while (Match(TokenType::PUNCTUATION, ","))
            stmt->column_names.push_back(Expect(TokenType::IDENTIFIER).value);
        Expect(TokenType::PUNCTUATION, ")");
    }

    Expect(TokenType::KEYWORD, "VALUES");

    // One or more value rows.
    auto ParseRow = [&]() {
        std::vector<std::unique_ptr<Expr>> row;
        Expect(TokenType::PUNCTUATION, "(");
        row.push_back(ParseExpr());
        while (Match(TokenType::PUNCTUATION, ","))
            row.push_back(ParseExpr());
        Expect(TokenType::PUNCTUATION, ")");
        return row;
    };

    stmt->values.push_back(ParseRow());
    while (Match(TokenType::PUNCTUATION, ","))
        stmt->values.push_back(ParseRow());

    return stmt;
}

// ---------------------------------------------------------------------------
// UPDATE
// ---------------------------------------------------------------------------

std::unique_ptr<UpdateStatement> Parser::ParseUpdate() {
    Expect(TokenType::KEYWORD, "UPDATE");
    auto stmt = std::make_unique<UpdateStatement>();

    ParseQualifiedName(stmt->schema_name, stmt->table_name);

    Expect(TokenType::KEYWORD, "SET");

    auto ParseSetClause = [&]() {
        UpdateSetClause clause;
        clause.column_name = Expect(TokenType::IDENTIFIER).value;
        Expect(TokenType::OPERATOR, "=");
        clause.value_expr = ParseExpr();
        return clause;
    };

    stmt->set_clauses.push_back(ParseSetClause());
    while (Match(TokenType::PUNCTUATION, ","))
        stmt->set_clauses.push_back(ParseSetClause());

    if (Match(TokenType::KEYWORD, "WHERE"))
        stmt->where_clause = ParseExpr();

    return stmt;
}

// ---------------------------------------------------------------------------
// DELETE
// ---------------------------------------------------------------------------

std::unique_ptr<DeleteStatement> Parser::ParseDelete() {
    Expect(TokenType::KEYWORD, "DELETE");
    Expect(TokenType::KEYWORD, "FROM");
    auto stmt = std::make_unique<DeleteStatement>();

    ParseQualifiedName(stmt->schema_name, stmt->table_name);

    if (Match(TokenType::KEYWORD, "WHERE"))
        stmt->where_clause = ParseExpr();

    return stmt;
}

// ---------------------------------------------------------------------------
// CREATE TABLE
// ---------------------------------------------------------------------------

std::unique_ptr<CreateTableStatement> Parser::ParseCreateTable() {
    Expect(TokenType::KEYWORD, "CREATE");
    Expect(TokenType::KEYWORD, "TABLE");
    auto stmt = std::make_unique<CreateTableStatement>();

    if (Match(TokenType::KEYWORD, "IF")) {
        Expect(TokenType::KEYWORD, "NOT");
        Expect(TokenType::KEYWORD, "EXISTS");
        stmt->if_not_exists = true;
    }

    ParseQualifiedName(stmt->schema_name, stmt->table_name);

    Expect(TokenType::PUNCTUATION, "(");

    auto ParseColDef = [&]() {
        ColumnDef col;
        col.name = Expect(TokenType::IDENTIFIER).value;
        col.type = ParseTypeName();
        while (Check(TokenType::KEYWORD, "NOT") || Check(TokenType::KEYWORD, "PRIMARY")) {
            if (Match(TokenType::KEYWORD, "NOT")) {
                Expect(TokenType::KEYWORD, "NULL");
                col.not_null = true;
            } else if (Match(TokenType::KEYWORD, "PRIMARY")) {
                Expect(TokenType::KEYWORD, "KEY");
                col.primary_key = true;
                col.not_null    = true; // PRIMARY KEY implies NOT NULL
            }
        }
        return col;
    };

    stmt->columns.push_back(ParseColDef());
    while (Match(TokenType::PUNCTUATION, ","))
        stmt->columns.push_back(ParseColDef());

    Expect(TokenType::PUNCTUATION, ")");
    return stmt;
}

// ---------------------------------------------------------------------------
// DROP TABLE
// ---------------------------------------------------------------------------

std::unique_ptr<DropTableStatement> Parser::ParseDropTable() {
    Expect(TokenType::KEYWORD, "DROP");
    Expect(TokenType::KEYWORD, "TABLE");
    auto stmt = std::make_unique<DropTableStatement>();

    if (Match(TokenType::KEYWORD, "IF")) {
        Expect(TokenType::KEYWORD, "EXISTS");
        stmt->if_exists = true;
    }

    ParseQualifiedName(stmt->schema_name, stmt->table_name);
    return stmt;
}

// ---------------------------------------------------------------------------
// Transaction statements (BEGIN / COMMIT / ROLLBACK)
// ---------------------------------------------------------------------------

std::unique_ptr<TransactionStatement> Parser::ParseTransaction(ParsedStatement::Type type) {
    Consume(); // consume BEGIN / COMMIT / ROLLBACK
    auto stmt = std::make_unique<TransactionStatement>();
    stmt->stmt_type = type;
    return stmt;
}

// ---------------------------------------------------------------------------
// EXPLAIN [ANALYZE]
// ---------------------------------------------------------------------------

std::unique_ptr<ExplainStatement> Parser::ParseExplain() {
    Expect(TokenType::KEYWORD, "EXPLAIN");
    auto stmt = std::make_unique<ExplainStatement>();

    if (Match(TokenType::KEYWORD, "ANALYZE"))
        stmt->analyze = true;

    stmt->inner = ParseStatement(); // parse inner statement without trailing check
    return stmt;
}

// ---------------------------------------------------------------------------
// Expression parsing (recursive descent)
// ---------------------------------------------------------------------------

std::unique_ptr<Expr> Parser::ParseExpr() {
    return ParseOr();
}

std::unique_ptr<Expr> Parser::ParseOr() {
    auto left = ParseAnd();
    while (Check(TokenType::KEYWORD, "OR")) {
        Consume();
        auto right = ParseAnd();
        auto node  = std::make_unique<BinaryOpExpr>();
        node->op    = "OR";
        node->left  = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

std::unique_ptr<Expr> Parser::ParseAnd() {
    auto left = ParseNot();
    while (Check(TokenType::KEYWORD, "AND")) {
        Consume();
        auto right = ParseNot();
        auto node  = std::make_unique<BinaryOpExpr>();
        node->op    = "AND";
        node->left  = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

std::unique_ptr<Expr> Parser::ParseNot() {
    if (Check(TokenType::KEYWORD, "NOT")) {
        Consume();
        auto node    = std::make_unique<UnaryOpExpr>();
        node->op     = "NOT";
        node->operand = ParseNot();
        return node;
    }
    return ParseComparison();
}

std::unique_ptr<Expr> Parser::ParseComparison() {
    auto left = ParseAddSub();
    while (Check(TokenType::OPERATOR, "=")  || Check(TokenType::OPERATOR, "<")  ||
           Check(TokenType::OPERATOR, ">")  || Check(TokenType::OPERATOR, "<=") ||
           Check(TokenType::OPERATOR, ">=") || Check(TokenType::OPERATOR, "<>")) {
        Token op = Consume();
        auto right = ParseAddSub();
        auto node  = std::make_unique<BinaryOpExpr>();
        node->op    = op.value;
        node->left  = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

std::unique_ptr<Expr> Parser::ParseAddSub() {
    auto left = ParseMulDiv();
    while (Check(TokenType::OPERATOR, "+") || Check(TokenType::OPERATOR, "-")) {
        Token op = Consume();
        auto right = ParseMulDiv();
        auto node  = std::make_unique<BinaryOpExpr>();
        node->op    = op.value;
        node->left  = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

std::unique_ptr<Expr> Parser::ParseMulDiv() {
    auto left = ParseUnary();
    while (Check(TokenType::OPERATOR, "*") || Check(TokenType::OPERATOR, "/")) {
        Token op = Consume();
        auto right = ParseUnary();
        auto node  = std::make_unique<BinaryOpExpr>();
        node->op    = op.value;
        node->left  = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

std::unique_ptr<Expr> Parser::ParseUnary() {
    if (Check(TokenType::OPERATOR, "-")) {
        Consume();
        auto node    = std::make_unique<UnaryOpExpr>();
        node->op     = "-";
        node->operand = ParseUnary();
        return node;
    }
    return ParsePrimary();
}

std::unique_ptr<Expr> Parser::ParsePrimary() {
    const Token& tok = Peek();

    // Parenthesised expression.
    if (Check(TokenType::PUNCTUATION, "(")) {
        Consume();
        auto expr = ParseExpr();
        Expect(TokenType::PUNCTUATION, ")");
        return expr;
    }

    // Star — SELECT * or COUNT(*).
    if (Check(TokenType::OPERATOR, "*")) {
        Consume();
        return std::make_unique<StarExpr>();
    }

    // Integer literal.
    if (tok.type == TokenType::INTEGER_LIT) {
        Consume();
        auto node = std::make_unique<IntegerLitExpr>();
        node->value = std::stoll(tok.value);
        return node;
    }

    // Float literal.
    if (tok.type == TokenType::FLOAT_LIT) {
        Consume();
        auto node = std::make_unique<FloatLitExpr>();
        node->value = std::stod(tok.value);
        return node;
    }

    // String literal.
    if (tok.type == TokenType::STRING_LIT) {
        Consume();
        auto node = std::make_unique<StringLitExpr>();
        node->value = tok.value;
        return node;
    }

    // TRUE / FALSE.
    if (Check(TokenType::KEYWORD, "TRUE")) {
        Consume();
        auto node = std::make_unique<BoolLitExpr>();
        node->value = true;
        return node;
    }
    if (Check(TokenType::KEYWORD, "FALSE")) {
        Consume();
        auto node = std::make_unique<BoolLitExpr>();
        node->value = false;
        return node;
    }

    // NULL.
    if (Check(TokenType::KEYWORD, "NULL")) {
        Consume();
        return std::make_unique<NullLitExpr>();
    }

    // Identifier: column reference or function call.
    if (tok.type == TokenType::IDENTIFIER) {
        std::string name = tok.value;
        size_t      name_pos = tok.pos;
        Consume();

        // Function call: name(args...)
        if (Check(TokenType::PUNCTUATION, "(")) {
            Consume();
            auto node = std::make_unique<FunctionCallExpr>();
            node->name = ToUpper(name);

            if (Check(TokenType::PUNCTUATION, ")")) {
                // Zero-arg call.
            } else if (Check(TokenType::OPERATOR, "*")) {
                // COUNT(*) — special case.
                Consume();
                node->is_star = true;
            } else {
                node->args.push_back(ParseExpr());
                while (Match(TokenType::PUNCTUATION, ","))
                    node->args.push_back(ParseExpr());
            }
            Expect(TokenType::PUNCTUATION, ")");
            return node;
        }

        // Qualified column ref: table.column
        if (Check(TokenType::PUNCTUATION, ".")) {
            Consume();
            std::string col = Expect(TokenType::IDENTIFIER).value;
            auto node = std::make_unique<ColumnRefExpr>();
            node->table_name  = name;
            node->column_name = col;
            return node;
        }

        // Simple column ref.
        auto node = std::make_unique<ColumnRefExpr>();
        node->column_name = name;
        return node;
    }

    throw ParseError("unexpected token '" + tok.value + "' in expression", tok.pos);
}

} // namespace cppcoldb
