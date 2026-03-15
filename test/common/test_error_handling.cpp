#include "common/exception.hpp"

#include <cassert>
#include <iostream>
#include <string>

using namespace cppcoldb;

static void TestParseErrorThrown() {
    bool caught = false;
    try {
        throw ParseError("unexpected token 'GARBAGE'");
    } catch (const ParseError& e) {
        caught = true;
        assert(std::string(e.what()) == "unexpected token 'GARBAGE'");
        assert(e.Type() == "ParseError");
    }
    assert(caught);
    std::cout << "TestParseErrorThrown: PASS\n";
}

static void TestParseErrorPosition() {
    ParseError e("unexpected token", 12);
    assert(e.Position() == 12);

    ParseError e_no_pos("no position");
    assert(e_no_pos.Position() == std::string::npos);
    std::cout << "TestParseErrorPosition: PASS\n";
}

static void TestBindErrorType() {
    bool caught = false;
    try {
        throw BindError("table 'orders' not found");
    } catch (const BindError& e) {
        caught = true;
        assert(e.Type() == "BindError");
        assert(e.Message() == "table 'orders' not found");
    }
    assert(caught);
    std::cout << "TestBindErrorType: PASS\n";
}

static void TestRuntimeErrorType() {
    bool caught = false;
    try {
        throw RuntimeError("division by zero");
    } catch (const RuntimeError& e) {
        caught = true;
        assert(e.Type() == "RuntimeError");
    }
    assert(caught);
    std::cout << "TestRuntimeErrorType: PASS\n";
}

static void TestIOErrorType() {
    bool caught = false;
    try {
        throw IOError("disk full");
    } catch (const IOError& e) {
        caught = true;
        assert(e.Type() == "IOError");
    }
    assert(caught);
    std::cout << "TestIOErrorType: PASS\n";
}

static void TestExceptionBaseClass() {
    // All concrete types catchable as CppColDBException
    try {
        throw RuntimeError("base catch test");
    } catch (const CppColDBException& e) {
        assert(e.Type() == "RuntimeError");
    }

    // Also catchable as std::exception
    try {
        throw IOError("std catch test");
    } catch (const std::exception& e) {
        assert(std::string(e.what()) == "std catch test");
    }
    std::cout << "TestExceptionBaseClass: PASS\n";
}

static void TestExceptionTypeNames() {
    assert(ParseError("x").Type()   == "ParseError");
    assert(BindError("x").Type()    == "BindError");
    assert(RuntimeError("x").Type() == "RuntimeError");
    assert(IOError("x").Type()      == "IOError");
    std::cout << "TestExceptionTypeNames: PASS\n";
}

static void TestMacros() {
    bool caught = false;
    try {
        THROW_PARSE_ERROR("bad sql", 5);
    } catch (const ParseError& e) {
        caught = true;
        assert(e.Position() == 5);
    }
    assert(caught);

    caught = false;
    try { THROW_BIND_ERROR("no table"); } catch (const BindError&) { caught = true; }
    assert(caught);

    caught = false;
    try { THROW_RUNTIME_ERROR("overflow"); } catch (const RuntimeError&) { caught = true; }
    assert(caught);

    caught = false;
    try { THROW_IO_ERROR("read failed"); } catch (const IOError&) { caught = true; }
    assert(caught);

    std::cout << "TestMacros: PASS\n";
}

void RunErrorHandlingTests() {
    TestParseErrorThrown();
    TestParseErrorPosition();
    TestBindErrorType();
    TestRuntimeErrorType();
    TestIOErrorType();
    TestExceptionBaseClass();
    TestExceptionTypeNames();
    TestMacros();

    std::cout << "\nAll test_error_handling tests passed.\n";
}
