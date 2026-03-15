#include "parser/tokenizer.hpp"
#include "common/exception.hpp"

#include <cassert>
#include <iostream>

using namespace cppcoldb;

static void TestTokenizerSimpleSelect() {
    Tokenizer tok("SELECT a FROM t");
    auto tokens = tok.Tokenize();
    // SELECT, a, FROM, t, END_OF_INPUT
    assert(tokens.size() == 5);
    assert(tokens[0].type == TokenType::KEYWORD);    assert(tokens[0].value == "SELECT");
    assert(tokens[1].type == TokenType::IDENTIFIER); assert(tokens[1].value == "a");
    assert(tokens[2].type == TokenType::KEYWORD);    assert(tokens[2].value == "FROM");
    assert(tokens[3].type == TokenType::IDENTIFIER); assert(tokens[3].value == "t");
    assert(tokens[4].type == TokenType::END_OF_INPUT);
    std::cout << "TestTokenizerSimpleSelect: PASS\n";
}

static void TestTokenizerStringLiteral() {
    Tokenizer tok("'hello world'");
    auto tokens = tok.Tokenize();
    assert(tokens.size() == 2);
    assert(tokens[0].type == TokenType::STRING_LIT);
    assert(tokens[0].value == "hello world");
    std::cout << "TestTokenizerStringLiteral: PASS\n";
}

static void TestTokenizerFloatLiteral() {
    Tokenizer tok("3.14");
    auto tokens = tok.Tokenize();
    assert(tokens.size() == 2);
    assert(tokens[0].type == TokenType::FLOAT_LIT);
    assert(tokens[0].value == "3.14");
    std::cout << "TestTokenizerFloatLiteral: PASS\n";
}

static void TestTokenizerTwoCharOp() {
    for (const char* op : {"<>", "<=", ">="}) {
        Tokenizer tok(op);
        auto tokens = tok.Tokenize();
        assert(tokens.size() == 2);
        assert(tokens[0].type == TokenType::OPERATOR);
        assert(tokens[0].value == op);
    }
    std::cout << "TestTokenizerTwoCharOp: PASS\n";
}

static void TestTokenizerUnknownChar() {
    bool threw = false;
    try {
        Tokenizer tok("SELECT @foo FROM t");
        tok.Tokenize();
    } catch (const ParseError& e) {
        threw = true;
        assert(e.Position() != std::string::npos);
    }
    assert(threw);
    std::cout << "TestTokenizerUnknownChar: PASS\n";
}

static void TestTokenizerIntegerLiteral() {
    Tokenizer tok("42");
    auto tokens = tok.Tokenize();
    assert(tokens[0].type == TokenType::INTEGER_LIT);
    assert(tokens[0].value == "42");
    std::cout << "TestTokenizerIntegerLiteral: PASS\n";
}

static void TestTokenizerPunctuation() {
    Tokenizer tok("(,);.");
    auto tokens = tok.Tokenize();
    assert(tokens[0].value == "(");
    assert(tokens[1].value == ",");
    assert(tokens[2].value == ")");
    assert(tokens[3].value == ";");
    assert(tokens[4].value == ".");
    std::cout << "TestTokenizerPunctuation: PASS\n";
}

static void TestTokenizerCaseInsensitiveKeywords() {
    Tokenizer tok("select a from t");
    auto tokens = tok.Tokenize();
    assert(tokens[0].type == TokenType::KEYWORD);
    assert(tokens[0].value == "SELECT");
    assert(tokens[2].type == TokenType::KEYWORD);
    assert(tokens[2].value == "FROM");
    std::cout << "TestTokenizerCaseInsensitiveKeywords: PASS\n";
}

static void TestTokenizerPositions() {
    Tokenizer tok("SELECT a");
    auto tokens = tok.Tokenize();
    assert(tokens[0].pos == 0); // SELECT starts at 0
    assert(tokens[1].pos == 7); // 'a' starts at 7
    std::cout << "TestTokenizerPositions: PASS\n";
}

void RunTokenizerTests() {
    TestTokenizerSimpleSelect();
    TestTokenizerStringLiteral();
    TestTokenizerFloatLiteral();
    TestTokenizerTwoCharOp();
    TestTokenizerUnknownChar();
    TestTokenizerIntegerLiteral();
    TestTokenizerPunctuation();
    TestTokenizerCaseInsensitiveKeywords();
    TestTokenizerPositions();
    std::cout << "\nAll tokenizer tests passed.\n";
}
