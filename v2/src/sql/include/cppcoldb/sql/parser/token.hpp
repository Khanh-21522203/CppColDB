#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace cppcoldb::sql::parser {

// Lexical category of a scanned Token.
enum class TokenType : std::uint8_t {
    KEYWORD,
    IDENTIFIER,
    INTEGER_LIT,
    FLOAT_LIT,
    STRING_LIT,
    OPERATOR,
    PUNCTUATION,
    END_OF_INPUT,
};

// A single lexed token from the SQL input.
struct Token {
    TokenType   type = TokenType::END_OF_INPUT;
    std::string value; // raw text; keywords are stored uppercase
    std::size_t pos = 0; // byte offset in the original SQL string
};

} // namespace cppcoldb::sql::parser
