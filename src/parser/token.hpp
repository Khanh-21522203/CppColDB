#pragma once
#include <string>
#include <cstdint>

namespace cppcoldb {

enum class TokenType : uint8_t {
    KEYWORD,
    IDENTIFIER,
    INTEGER_LIT,
    FLOAT_LIT,
    STRING_LIT,
    OPERATOR,
    PUNCTUATION,
    END_OF_INPUT,
};

struct Token {
    TokenType   type;
    std::string value; // raw text; keywords are stored uppercase
    size_t      pos;   // byte offset in original SQL string
};

} // namespace cppcoldb
