#pragma once
#include <string_view>
#include <vector>

#include "cppcoldb/sql/parser/token.hpp"

namespace cppcoldb::sql::parser {

// Scans raw SQL text into a stream of Tokens.
class Tokenizer {
public:
    explicit Tokenizer(std::string_view sql);

    // Scan the full input and return a token stream ending with END_OF_INPUT.
    // Throws common::ParseError on any unrecognised character.
    std::vector<Token> Tokenize();

private:
    std::string_view sql_;
};

} // namespace cppcoldb::sql::parser
