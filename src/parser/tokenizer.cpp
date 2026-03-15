#include "parser/tokenizer.hpp"
#include "common/exception.hpp"

#include <cctype>
#include <unordered_set>
#include <algorithm>

namespace cppcoldb {

// All SQL keywords recognised by this tokenizer (stored uppercase).
static const std::unordered_set<std::string> KEYWORD_SET = {
    "SELECT", "FROM", "WHERE", "INSERT", "INTO", "UPDATE", "SET",
    "DELETE", "CREATE", "TABLE", "DROP", "BEGIN", "COMMIT", "ROLLBACK",
    "EXPLAIN", "ANALYZE", "AND", "OR", "NOT", "NULL", "TRUE", "FALSE",
    "GROUP", "BY", "ORDER", "ASC", "DESC", "LIMIT", "OFFSET",
    "VALUES", "PRIMARY", "KEY", "DISTINCT", "IF", "EXISTS",
    "JOIN", "ON", "INNER", "LEFT", "RIGHT", "OUTER", "CROSS",
    "AS", "IN", "IS", "LIKE", "BETWEEN", "HAVING",
    "INT8", "INT16", "INT32", "INT64",
    "FLOAT32", "FLOAT64", "BOOLEAN", "VARCHAR",
    "INT", "INTEGER", "FLOAT", "DOUBLE", "BOOL", "TEXT",
};

Tokenizer::Tokenizer(std::string_view sql) : sql_(sql) {}

std::vector<Token> Tokenizer::Tokenize() {
    std::vector<Token> tokens;
    size_t pos = 0;

    while (pos < sql_.size()) {
        // Skip whitespace.
        if (std::isspace(static_cast<unsigned char>(sql_[pos]))) {
            ++pos;
            continue;
        }

        size_t start = pos;
        char c = sql_[pos];

        // Identifier or keyword.
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            while (pos < sql_.size() &&
                   (std::isalnum(static_cast<unsigned char>(sql_[pos])) || sql_[pos] == '_')) {
                ++pos;
            }
            std::string word(sql_.substr(start, pos - start));
            std::string upper = word;
            for (char& ch : upper) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));

            if (KEYWORD_SET.count(upper)) {
                tokens.push_back({TokenType::KEYWORD, upper, start});
            } else {
                tokens.push_back({TokenType::IDENTIFIER, word, start});
            }
            continue;
        }

        // Numeric literal (integer or float).
        if (std::isdigit(static_cast<unsigned char>(c))) {
            while (pos < sql_.size() && std::isdigit(static_cast<unsigned char>(sql_[pos])))
                ++pos;

            bool is_float = false;
            if (pos < sql_.size() && sql_[pos] == '.' &&
                pos + 1 < sql_.size() && std::isdigit(static_cast<unsigned char>(sql_[pos + 1]))) {
                is_float = true;
                ++pos; // consume '.'
                while (pos < sql_.size() && std::isdigit(static_cast<unsigned char>(sql_[pos])))
                    ++pos;
            }

            std::string val(sql_.substr(start, pos - start));
            tokens.push_back({is_float ? TokenType::FLOAT_LIT : TokenType::INTEGER_LIT, val, start});
            continue;
        }

        // String literal (single-quoted).
        if (c == '\'') {
            ++pos; // skip opening quote
            std::string val;
            while (pos < sql_.size() && sql_[pos] != '\'') {
                // Minimal escape: '' → ' (SQL standard)
                if (sql_[pos] == '\'' && pos + 1 < sql_.size() && sql_[pos + 1] == '\'') {
                    val += '\'';
                    pos += 2;
                } else {
                    val += sql_[pos++];
                }
            }
            if (pos >= sql_.size())
                throw ParseError("unterminated string literal", start);
            ++pos; // skip closing quote
            tokens.push_back({TokenType::STRING_LIT, val, start});
            continue;
        }

        // Two-character operators.
        if (pos + 1 < sql_.size()) {
            std::string two(sql_.substr(pos, 2));
            if (two == "<=" || two == ">=" || two == "<>") {
                tokens.push_back({TokenType::OPERATOR, two, start});
                pos += 2;
                continue;
            }
        }

        // Single-character operators.
        if (c == '=' || c == '<' || c == '>' || c == '+' || c == '-' || c == '*' || c == '/') {
            tokens.push_back({TokenType::OPERATOR, std::string(1, c), start});
            ++pos;
            continue;
        }

        // Punctuation.
        if (c == '(' || c == ')' || c == ',' || c == ';' || c == '.') {
            tokens.push_back({TokenType::PUNCTUATION, std::string(1, c), start});
            ++pos;
            continue;
        }

        throw ParseError(std::string("unrecognized character '") + c + "' at position " +
                             std::to_string(pos),
                         pos);
    }

    tokens.push_back({TokenType::END_OF_INPUT, "", sql_.size()});
    return tokens;
}

} // namespace cppcoldb
