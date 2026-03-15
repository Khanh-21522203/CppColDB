#pragma once
#include <string>
#include <string_view>
#include <vector>
#include "parser/token.hpp"

namespace cppcoldb {

class Tokenizer {
public:
    explicit Tokenizer(std::string_view sql);

    // Scan the full input and return a token stream ending with END_OF_INPUT.
    // Throws ParseError on any unrecognised character.
    std::vector<Token> Tokenize();

private:
    std::string_view sql_;
};

} // namespace cppcoldb
